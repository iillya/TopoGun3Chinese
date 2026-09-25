#include "../qt_compatibility.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto error = TopoGunQtCompatibility::missingQtImport(argv[1], argv[2]);
    std::wcout << L"family=" << TopoGunQtCompatibility::completeFamily(argv[2], 5)
               << L" imports=" << (error.empty() ? L"PASS" : error) << L"\n";
    return error.empty() ? 0 : 1;
}
