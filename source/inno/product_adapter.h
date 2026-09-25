// TopoGun-specific adapter for the installer directory checks.
#include "product_config.h"

std::wstring hostName(const std::wstring&) { return kHostPattern; }
bool productQt(const std::wstring& root) {
    return TopoGunQtCompatibility::completeFamily(root, 5);
}
