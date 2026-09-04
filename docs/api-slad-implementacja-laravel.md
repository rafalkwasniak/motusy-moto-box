# Ślad trasy — instrukcja wdrożenia po stronie Laravela

Dokument do wzięcia do projektu strony. Opisuje **co zbudować**, żeby obsłużyć
ślad trasy z Motusy Moto Box. Kontrakt (czyli co dokładnie przychodzi po kablu)
jest w [`api-slad-trasy.md`](api-slad-trasy.md) — tutaj jest strona serwerowa.

Ślad jest **niezależnym dodatkiem** do istniejącego `POST /api/v1/rides`.
Nic w tamtym endpoincie się nie zmienia i **brak śladu nigdy nie może blokować
`accepted_through`**.

---

## 0. Skrót dla niecierpliwych

```
POST /api/v1/devices/{device_id}/rides/{seq}/track
Authorization: Bearer <token konta>
Content-Type: text/plain; charset=us-ascii
```

Ciało to plik tekstowy (nie JSON), do ~50 kB, wyjątkowo do 1 MB. Klucz
idempotencji `(device_id, seq)`. Odpowiedź `{"stored": true}`. Ślad **może
przyjść zanim przyjdzie przejazd o tym `seq`** i wtedy też trzeba go przyjąć.

Do zrobienia: migracja, trasa, kontroler, parser, eksport GPX. Poniżej po kolei.

---

## 1. Migracja

Dwie decyzje warte uzasadnienia.

**Surowa treść ląduje na dysku, nie w kolumnie.** Ślad to kilkadziesiąt
kilobajtów; trzymany w `longText` obciąża każde zapytanie listujące przejazdy,
nawet gdy nikt nie otwiera mapy. Plik na prywatnym dysku zostaje źródłem prawdy,
z którego można przeliczyć wszystko od nowa, gdy parser się poprawi.

**Statystyki liczymy raz, przy przyjęciu.** Dystans, liczba punktów, zakres
czasu i prostokąt otaczający są potrzebne na liście przejazdów. Liczenie ich
przy każdym wyświetleniu znaczyłoby parsowanie pliku przy każdym odświeżeniu
strony.

```php
Schema::create('ride_tracks', function (Blueprint $table) {
    $table->id();
    $table->foreignId('user_id')->constrained();

    // Klucz idempotencji — ten sam co przy przejazdach.
    $table->string('device_id', 12);
    $table->unsignedInteger('seq');

    // NULLABLE CELOWO: ślad może dojść przed wynikiem przejazdu, bo to dwa
    // niezależne żądania i o kolejności decyduje moment złapania sieci.
    $table->foreignId('ride_id')->nullable()->constrained()->nullOnDelete();

    $table->string('path');                 // ścieżka do surowego pliku
    $table->unsignedInteger('bytes');
    $table->string('format', 8);            // "MMBT1"
    $table->string('fw', 16);
    $table->unsignedTinyInteger('corridor_m');

    // Statystyki policzone przy przyjęciu.
    $table->unsignedInteger('point_count');
    $table->unsignedSmallInteger('segment_count');
    $table->unsignedInteger('distance_m');
    $table->unsignedInteger('started_at')->nullable();   // unix UTC, null = czas nieznany
    $table->unsignedInteger('ended_at')->nullable();
    $table->decimal('min_lat', 9, 6)->nullable();
    $table->decimal('max_lat', 9, 6)->nullable();
    $table->decimal('min_lon', 9, 6)->nullable();
    $table->decimal('max_lon', 9, 6)->nullable();
    $table->smallInteger('max_lean_deg')->nullable();

    $table->timestamps();
    $table->softDeletes();

    $table->unique(['device_id', 'seq']);
});
```

Dysk w `config/filesystems.php` — **prywatny, nie `public`**. Ślady są widoczne
wyłącznie dla właściciela konta, więc plik nie może mieć adresu, który da się
zgadnąć:

```php
'tracks' => [
    'driver' => 'local',
    'root'   => storage_path('app/tracks'),
    'visibility' => 'private',
],
```

---

### Powiązania w modelach

```php
// App\Models\Ride
public function track(): HasOne
{
    return $this->hasOne(RideTrack::class);
}

// App\Models\RideTrack
protected $fillable = [
    'user_id', 'device_id', 'seq', 'ride_id', 'path', 'bytes', 'format', 'fw',
    'corridor_m', 'point_count', 'segment_count', 'distance_m',
    'started_at', 'ended_at', 'min_lat', 'max_lat', 'min_lon', 'max_lon', 'max_lean_deg',
];

public function ride(): BelongsTo
{
    return $this->belongsTo(Ride::class);
}
```

`started_at` i `ended_at` zostawiam jako `unsignedInteger` (unix UTC), a nie
`datetime` — dokładnie tak jak `recorded_at` w `rides`. Powód jest ten sam:
urządzenie nie zna swojej strefy, a `null` znaczy „czasu nie było", nie
„początek epoki".

---

## 2. Trasa

```php
// routes/api.php — obok istniejącego /v1/rides
Route::middleware('auth:sanctum')->prefix('v1')->group(function () {
    Route::post('/devices/{deviceId}/rides/{seq}/track', [RideTrackController::class, 'store'])
        ->where(['deviceId' => '[0-9a-f]{12}', 'seq' => '[0-9]+']);

    // Do podglądu na stronie — te dwa są dla przeglądarki, nie dla urządzenia.
    Route::get('/rides/{ride}/track', [RideTrackController::class, 'show']);
    Route::get('/rides/{ride}/track.gpx', [RideTrackController::class, 'gpx']);
});
```

To ten sam middleware, którym chroniony jest `POST /v1/rides`.

### Limity ciała żądania

Ciało bywa większe niż typowy formularz, więc warto sprawdzić dwa miejsca,
zanim pojawi się tajemnicze 413 z serwera WWW, a nie z aplikacji:

- nginx: `client_max_body_size 2m;`
- PHP: `post_max_size` co najmniej `2M` (domyślne 8M zwykle wystarcza)

---

## 3. Parser

Osobna klasa, bez zależności od HTTP — dzięki temu da się ją przetestować na
napisie i użyć ponownie przy przeliczaniu starych śladów.

```php
namespace App\Services;

class TrackParser
{
    public const FORMAT = 'MMBT1';

    /**
     * @return array{header: array<string,string>, segments: list<list<array>>}
     * @throws TrackFormatException
     */
    public function parse(string $body): array
    {
        $lines = explode("\n", $body);

        if (($lines[0] ?? '') !== self::FORMAT) {
            throw new TrackFormatException('nieznana wersja formatu sladu');
        }

        // Nagłówek: klucz=wartość, jedna para na linię, pusta linia kończy.
        $header = [];
        $i = 1;
        for (; $i < count($lines) && $lines[$i] !== ''; $i++) {
            if (! str_contains($lines[$i], '=')) {
                throw new TrackFormatException("zla linia naglowka: {$lines[$i]}");
            }
            [$key, $value] = explode('=', $lines[$i], 2);
            $header[$key] = $value;
        }
        $i++;

        foreach (['dev', 'fw', 'eps', 't0', 'p0'] as $required) {
            if (! isset($header[$required])) {
                throw new TrackFormatException("brak pola naglowka: {$required}");
            }
        }

        // UWAGA: lon jest PIERWSZE. Zamiana daje ślad w Somalii.
        [$lon, $lat] = array_map('intval', explode(',', $header['p0']));
        $time = (int) $header['t0'];

        $segments = [];
        $current  = [$this->point($lon, $lat, $time, null)];

        for (; $i < count($lines); $i++) {
            $row = $lines[$i];

            if ($row === '') {
                continue;               // końcowa nowa linia
            }

            if ($row === '-') {
                // "Podnieś ołówek". Sumowanie NIE jest zerowane — znacznik mówi
                // tylko tyle, że między tymi punktami nie było jazdy.
                $segments[] = $current;
                $current = [];
                continue;
            }

            $parts = explode(',', $row);
            if (count($parts) !== 4) {
                throw new TrackFormatException("zla linia punktu: {$row}");
            }

            [$dlon, $dlat, $dt, $lean] = array_map('intval', $parts);

            $lon  += $dlon;
            $lat  += $dlat;
            $time += $dt;

            $current[] = $this->point($lon, $lat, $time, $lean);
        }

        $segments[] = $current;

        return ['header' => $header, 'segments' => array_values(array_filter($segments))];
    }

    private function point(int $lonE5, int $latE5, int $time, ?int $lean): array
    {
        return [
            'lon'  => $lonE5 / 1e5,
            'lat'  => $latE5 / 1e5,
            'at'   => $time ?: null,   // 0 znaczy "czas nieznany", nie rok 1970
            'lean' => $lean,           // + = w prawo; null tylko w punkcie startowym
        ];
    }
}
```

### Statystyki

```php
class TrackStats
{
    public function summarize(array $segments): array
    {
        $points = 0; $distance = 0.0; $maxLean = null;
        $minLat = $maxLat = $minLon = $maxLon = null;
        $first = $last = null;

        foreach ($segments as $segment) {
            $previous = null;

            foreach ($segment as $point) {
                $points++;

                $minLat = $minLat === null ? $point['lat'] : min($minLat, $point['lat']);
                $maxLat = $maxLat === null ? $point['lat'] : max($maxLat, $point['lat']);
                $minLon = $minLon === null ? $point['lon'] : min($minLon, $point['lon']);
                $maxLon = $maxLon === null ? $point['lon'] : max($maxLon, $point['lon']);

                if ($point['lean'] !== null
                    && ($maxLean === null || abs($point['lean']) > abs($maxLean))) {
                    $maxLean = $point['lean'];
                }

                if ($point['at'] !== null) {
                    $first ??= $point['at'];
                    $last = $point['at'];
                }

                // Dystans TYLKO wewnątrz segmentu — przez przerwę motocykl nie
                // przejechał po linii prostej, więc doliczanie jej zawyżałoby wynik.
                if ($previous !== null) {
                    $distance += $this->meters($previous, $point);
                }
                $previous = $point;
            }
        }

        return [
            'point_count'    => $points,
            'segment_count'  => count($segments),
            'distance_m'     => (int) round($distance),
            'started_at'     => $first,
            'ended_at'       => $last,
            'min_lat' => $minLat, 'max_lat' => $maxLat,
            'min_lon' => $minLon, 'max_lon' => $maxLon,
            'max_lean_deg'   => $maxLean,
        ];
    }

    private function meters(array $a, array $b): float
    {
        // Płaska siatka lokalna wystarcza: odcinki mają najwyżej kilkaset metrów.
        $latM = 111_320.0;
        $lonM = $latM * cos(deg2rad($a['lat']));

        $dx = ($b['lon'] - $a['lon']) * $lonM;
        $dy = ($b['lat'] - $a['lat']) * $latM;

        return sqrt($dx * $dx + $dy * $dy);
    }
}
```

**Dystans z sumy odcinków zaniża wynik na zakrętach** (cięciwa zamiast łuku).
Przy korytarzu ε=8 m błąd jest rzędu 1% — dla licznika kilometrów bez znaczenia,
ale nie nadaje się jako „długość przejazdu co do metra".

---

## 4. Kontroler

```php
public function store(Request $request, string $deviceId, int $seq)
{
    $user = $request->user();
    $body = $request->getContent();

    // Ciało jest tekstem, więc walidacja jest ręczna — FormRequest nie ma
    // czego sprawdzać.
    if ($body === '') {
        abort(422, 'puste cialo zadania');
    }
    if (strlen($body) > 1_048_576) {
        abort(413, 'slad za duzy');
    }

    try {
        $parsed = app(TrackParser::class)->parse($body);
    } catch (TrackFormatException $e) {
        // 422 KASUJE plik w urządzeniu. Zwracać go wyłącznie wtedy, gdy
        // ponowienie faktycznie nic nie da.
        abort(422, $e->getMessage());
    }

    if (($parsed['header']['dev'] ?? null) !== $deviceId) {
        abort(422, 'device_id w naglowku nie zgadza sie z adresem');
    }

    $stats = app(TrackStats::class)->summarize($parsed['segments']);

    // Ślad może dojść przed przejazdem — wtedy ride_id zostaje null i podepnie
    // się później. Odsyłanie 404 zakleszczyłoby wysyłkę.
    $ride = Ride::where('user_id', $user->id)
        ->where('device_id', $deviceId)
        ->where('seq', $seq)
        ->first();

    $path = "tracks/{$user->id}/{$deviceId}/{$seq}.mmbt";
    Storage::disk('tracks')->put($path, $body);

    RideTrack::updateOrCreate(                      // powtórka to sukces
        ['device_id' => $deviceId, 'seq' => $seq],
        [
            'user_id'     => $user->id,
            'ride_id'     => $ride?->id,
            'path'        => $path,
            'bytes'       => strlen($body),
            'format'      => TrackParser::FORMAT,
            'fw'          => $parsed['header']['fw'],
            'corridor_m'  => (int) $parsed['header']['eps'],
            ...$stats,
        ],
    );

    return response()->json(['stored' => true]);
}
```

### Podpięcie śladu, który przyszedł pierwszy

W kontrolerze przejazdów, zaraz po `updateOrCreate` przejazdu:

```php
RideTrack::where('user_id', $user->id)
    ->where('device_id', $data['device_id'])
    ->where('seq', $ride['seq'])
    ->whereNull('ride_id')
    ->update(['ride_id' => $created->id]);
```

### Mapowanie błędów — to jest ważne

| Sytuacja | Kod | Dlaczego akurat ten |
|---|---|---|
| zapisano (także powtórka) | 200 | urządzenie kasuje plik |
| zły token | 401 | urządzenie **przestaje próbować** i pokazuje błąd |
| ciało > 1 MB | 413 | błąd trwały — urządzenie kasuje plik |
| zły format, zła wersja | 422 | błąd trwały — urządzenie kasuje plik |
| awaria bazy, dysku | 500 | urządzenie ponawia z rosnącym opóźnieniem |

**Nie zwracać 422 przy awarii przejściowej.** Urządzenie potraktuje to jako
„ten ślad nigdy nie przejdzie" i skasuje plik bezpowrotnie. W drugą stronę:
nie zwracać 500 przy trwale zepsutym pliku, bo urządzenie będzie budzić radio
w kółko aż do rozładowania baterii.

---

## 5. Eksport GPX

```php
public function gpx(Ride $ride)
{
    $this->authorize('view', $ride);

    $track = $ride->track()->firstOrFail();
    $parsed = app(TrackParser::class)->parse(Storage::disk('tracks')->get($track->path));

    $xml  = '<?xml version="1.0" encoding="UTF-8"?>' . "\n";
    $xml .= '<gpx version="1.1" creator="Motusy Moto Box" '
          . 'xmlns="http://www.topografix.com/GPX/1/1">' . "\n";
    $xml .= "<trk><name>Przejazd {$track->seq}</name>\n";

    foreach ($parsed['segments'] as $segment) {
        $xml .= "<trkseg>\n";
        foreach ($segment as $point) {
            $xml .= sprintf('<trkpt lat="%.5f" lon="%.5f">', $point['lat'], $point['lon']);
            if ($point['at'] !== null) {
                // Czas nieznany zapisujemy jako BRAK <time>, nie jako rok 1970 —
                // data 1970 myli programy do map bardziej niż jej brak.
                $xml .= '<time>' . gmdate('Y-m-d\TH:i:s\Z', $point['at']) . '</time>';
            }
            $xml .= "</trkpt>\n";
        }
        $xml .= "</trkseg>\n";
    }

    $xml .= "</trk></gpx>\n";

    return response($xml, 200, [
        'Content-Type'        => 'application/gpx+xml',
        'Content-Disposition' => "attachment; filename=\"przejazd-{$track->seq}.gpx\"",
    ]);
}
```

**Każdy segment to osobny `<trkseg>` w jednym `<trk>`.** To jest dokładnie ta
informacja, którą niesie linia `-`: między segmentami motocykl nie przejechał
po linii prostej, więc mapa nie ma prawa jej narysować.

Przechył nie ma odpowiednika w standardzie GPX. Jeśli ma trafić do pliku, to
przez `<extensions>` — ale do własnej mapy na stronie lepiej użyć JSON-a
z `show()`, bo tam kolorowanie linii kątem jest naturalne.

---

## 6. Testy

```php
it('przyjmuje slad i liczy statystyki', function () {
    $user = User::factory()->create();

    $body = "MMBT1\ndev=70041ddc6bc8\nfw=1.0.0\neps=8\nt0=1757001234\n"
          . "p0=1957648,5133528\n\n113,-182,5,12\n26,-3,1,-31\n-\n340,84,900,0\n";

    $this->actingAs($user)
        ->call('POST', '/api/v1/devices/70041ddc6bc8/rides/51/track', [], [], [],
               ['CONTENT_TYPE' => 'text/plain'], $body)
        ->assertOk()
        ->assertJson(['stored' => true]);

    $track = RideTrack::first();
    expect($track->point_count)->toBe(4)
        ->and($track->segment_count)->toBe(2)
        ->and($track->ride_id)->toBeNull();   // przejazd jeszcze nie doszedł
});

it('powtorka nie tworzy drugiego wpisu', function () { /* ten sam POST dwa razy */ });
it('odrzuca nieznana wersje formatu kodem 422', function () { /* "MMBT9\n\n" */ });
it('podpina slad, gdy przejazd dojdzie pozniej', function () { /* track, potem ride */ });
it('nie dolicza dystansu przez przerwe', function () { /* dwa odlegle segmenty */ });
```

Ostatni test jest wart osobnej uwagi: bez niego łatwo policzyć dystans przez
całą przerwę i dostać przejazd „o 40 km dłuższy", gdy ktoś przejechał tunelem.

---

## 7. Sprawdzenie curl-em

```bash
TOKEN=twoj-token-konta
BASE=https://motusy.top/api/v1

printf 'MMBT1\ndev=70041ddc6bc8\nfw=1.0.0\neps=8\nt0=1757001234\np0=1957648,5133528\n\n113,-182,5,12\n26,-3,1,-31\n-\n340,84,900,0\n' > /tmp/slad.txt

# 1. Zapis — oczekiwane 200 {"stored":true}
curl -i -X POST "$BASE/devices/70041ddc6bc8/rides/51/track" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: text/plain" \
  --data-binary @/tmp/slad.txt

# 2. Powtórka — oczekiwane 200 i JEDEN wpis w bazie
curl -s -X POST "$BASE/devices/70041ddc6bc8/rides/51/track" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: text/plain" \
  --data-binary @/tmp/slad.txt

# 3. Zła wersja formatu — oczekiwane 422, NIE 500
printf 'MMBT9\n\n' | curl -i -X POST "$BASE/devices/70041ddc6bc8/rides/51/track" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: text/plain" --data-binary @-

# 4. Zły token — oczekiwane 401
curl -i -X POST "$BASE/devices/70041ddc6bc8/rides/51/track" \
  -H "Authorization: Bearer zly-token" -H "Content-Type: text/plain" \
  --data-binary @/tmp/slad.txt
```

Ślad z przykładu odtwarza się na cztery punkty w dwóch segmentach:

```
51.33528 N, 19.57648 E   t=1757001234
51.33346 N, 19.57761 E   t=1757001239   przechył  +12°
51.33343 N, 19.57787 E   t=1757001240   przechył  -31°
   — przerwa —
51.33427 N, 19.58127 E   t=1757002140
```

To okolice Piotrkowa. **Jeśli wychodzi Ci Afryka, zamieniłeś `lon` z `lat`.**

---

## 8. Lista kontrolna

- [ ] migracja `ride_tracks` z unikalnym `(device_id, seq)` i **nullable** `ride_id`
- [ ] prywatny dysk `tracks` (nie `public`)
- [ ] trasa POST pod tym samym middleware co `/v1/rides`
- [ ] `client_max_body_size` w nginx podniesiony do 2 MB
- [ ] parser: `lon` przed `lat`, `-` nie zeruje sumowania
- [ ] statystyki liczone przy przyjęciu, dystans **bez** przerw
- [ ] `updateOrCreate` — powtórka to sukces, nie konflikt
- [ ] ślad przyjmowany, gdy przejazdu jeszcze nie ma; podpinany później
- [ ] 413 i 422 tylko przy błędach trwałych, 5xx przy przejściowych
- [ ] eksport GPX: segment = `<trkseg>`, brak czasu = brak `<time>`
- [ ] widok mapy dostępny wyłącznie dla właściciela konta
