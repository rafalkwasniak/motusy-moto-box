#include "MountCalibration.h"

namespace motion {

Vec3 axisVector(ForwardAxis axis) {
    switch (axis) {
        case ForwardAxis::DeviceXPlus:  return { 1.0f,  0.0f,  0.0f};
        case ForwardAxis::DeviceXMinus: return {-1.0f,  0.0f,  0.0f};
        case ForwardAxis::DeviceYPlus:  return { 0.0f,  1.0f,  0.0f};
        case ForwardAxis::DeviceYMinus: return { 0.0f, -1.0f,  0.0f};
        case ForwardAxis::DeviceZPlus:  return { 0.0f,  0.0f,  1.0f};
        case ForwardAxis::DeviceZMinus: return { 0.0f,  0.0f, -1.0f};
    }
    return {1.0f, 0.0f, 0.0f};
}

bool MountCalibration::calibrateFromRest(const Vec3& restingAccelG, ForwardAxis forward) {
    const float magnitude = restingAccelG.norm();

    // Motocykl musial stac nieruchomo — inaczej mierzymy nie tylko grawitacje.
    if (std::fabs(magnitude - 1.0f) > kAccelToleranceG) return false;

    // Akcelerometr w spoczynku wskazuje w GORE, os Z motocykla wskazuje w DOL.
    const Vec3 down = (restingAccelG * -1.0f).normalized();
    if (down.norm() < 0.5f) return false;

    // Trzeci stopien swobody z konwencji montazu: deklarowana os "przod",
    // zrzutowana na plaszczyzne prostopadla do zmierzonego pionu.
    const Vec3 hint = axisVector(forward);
    const Vec3 forwardRaw = hint - down * hint.dot(down);

    // Jesli deklarowana os "przod" jest niemal pionowa, rzut jest szumem.
    // sin(kat miedzy hint a pionem) == dlugosc rzutu, bo hint jest jednostkowy.
    if (forwardRaw.norm() < std::sin(kMinForwardTiltRad)) return false;

    const Vec3 fwd = forwardRaw.normalized();
    const Vec3 right = down.cross(fwd);

    // Wiersze macierzy to osie motocykla wyrazone w ukladzie urzadzenia,
    // dzieki czemu iloczyn macierz*wektor rzutuje wektor na te osie.
    rotation_.row0 = fwd;
    rotation_.row1 = right;
    rotation_.row2 = down;
    calibrated_ = true;
    return true;
}

void MountCalibration::restore(const Mat3& rotation) {
    rotation_ = rotation;
    calibrated_ = true;
}

void MountCalibration::reset() {
    rotation_ = Mat3{};
    calibrated_ = false;
}

}  // namespace motion
