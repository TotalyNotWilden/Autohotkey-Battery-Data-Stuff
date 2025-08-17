#include <windows.h>
#include <winioctl.h>
#include <batclass.h>
#include <setupapi.h>
#include <devguid.h>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

struct BatteryHealthInfo {
    double healthPercentage;
    ULONG chargeCycles;
    std::wstring activationDate;
    double ageInYears;
    std::wstring classification;
    bool isValid;
};

std::wstring GetWindowsActivationDate() {
    HKEY hKey;
    std::wstring activationDate = L"Unknown";

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        DWORD dataSize = sizeof(DWORD);
        DWORD installDate = 0;

        if (RegQueryValueEx(hKey, L"InstallDate", NULL, NULL,
            (LPBYTE)&installDate, &dataSize) == ERROR_SUCCESS) {

            // Convert Unix timestamp to Windows FILETIME
            LONGLONG ll = Int32x32To64(installDate, 10000000) + 116444736000000000;
            FILETIME ft;
            ft.dwLowDateTime = (DWORD)ll;
            ft.dwHighDateTime = (DWORD)(ll >> 32);

            // Convert to system time
            SYSTEMTIME st;
            if (FileTimeToSystemTime(&ft, &st)) {
                std::wstringstream ss;
                ss << std::setfill(L'0') << std::setw(2) << st.wMonth << L"/"
                    << std::setw(2) << st.wDay << L"/"
                    << st.wYear;
                activationDate = ss.str();
            }
        }
        RegCloseKey(hKey);
    }

    return activationDate;
}

double GetSystemAgeInYears() {
    HKEY hKey;
    double ageInYears = 0.0;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        DWORD dataSize = sizeof(DWORD);
        DWORD installDate = 0;

        if (RegQueryValueEx(hKey, L"InstallDate", NULL, NULL,
            (LPBYTE)&installDate, &dataSize) == ERROR_SUCCESS) {

            // Convert Unix timestamp to Windows FILETIME
            LONGLONG ll = Int32x32To64(installDate, 10000000) + 116444736000000000;
            FILETIME ft;
            ft.dwLowDateTime = (DWORD)ll;
            ft.dwHighDateTime = (DWORD)(ll >> 32);

            // Calculate age in years
            SYSTEMTIME currentTime;
            GetSystemTime(&currentTime);
            FILETIME currentFT;
            SystemTimeToFileTime(&currentTime, &currentFT);

            ULARGE_INTEGER currentULI, installULI;
            currentULI.LowPart = currentFT.dwLowDateTime;
            currentULI.HighPart = currentFT.dwHighDateTime;
            installULI.LowPart = ft.dwLowDateTime;
            installULI.HighPart = ft.dwHighDateTime;

            // Calculate difference in days and convert to years
            LONGLONG diffInDays = (currentULI.QuadPart - installULI.QuadPart) / (10000000LL * 60 * 60 * 24);
            ageInYears = (double)diffInDays / 365.25;
        }
        RegCloseKey(hKey);
    }

    return ageInYears;
}

std::wstring ClassifyBatteryHealth(double healthPercent, ULONG cycles, double ageInYears) {
    // Primary classification based on health percentage
    std::wstring classification;
    if (healthPercent >= 80.0) {
        classification = L"Good";
    }
    else if (healthPercent >= 60.0) {
        classification = L"Medium";
    }
    else {
        classification = L"Bad";
    }

    // Adjust based on charge cycles (most laptop batteries rated for 300-1000 cycles)
    if (cycles > 800) {
        if (classification == L"Good") {
            classification = L"Medium";
        }
        else if (classification == L"Medium") {
            classification = L"Bad";
        }
    }
    else if (cycles > 1000) {
        classification = L"Bad";
    }

    // Adjust based on age
    if (ageInYears > 4.0) {
        // Computer is old, expect some degradation
        classification = L"Bad";
    }
    else if (ageInYears > 2.0) {
        // 2-4 years old, downgrade if borderline
        if (classification == L"Good" && (healthPercent < 85.0 || cycles > 600)) {
            classification = L"Medium";
        }
        else if (classification == L"Medium") {
            classification = L"Bad";
        }
    }

    return classification;
}

BatteryHealthInfo EnumerateAndGetBatteryHealth() {
    BatteryHealthInfo info = { 0, 0, L"Unknown", 0.0, L"Unknown", false };

    // Get Windows activation/install date
    info.activationDate = GetWindowsActivationDate();
    info.ageInYears = GetSystemAgeInYears();

    // Get device information set for battery class devices
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY,
        NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return info;
    }

    // Enumerate battery devices
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData = { 0 };
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    DWORD deviceIndex = 0;
    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVCLASS_BATTERY,
        deviceIndex, &deviceInterfaceData)) {

        // Get the required buffer size
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &deviceInterfaceData,
            NULL, 0, &requiredSize, NULL);

        if (requiredSize > 0) {
            // Allocate buffer for device interface detail
            PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData =
                (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);

            if (pDeviceInterfaceDetailData) {
                pDeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                // Get device interface detail
                if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &deviceInterfaceData,
                    pDeviceInterfaceDetailData, requiredSize, NULL, NULL)) {

                    // Try to open this battery device
                    HANDLE hBattery = CreateFile(pDeviceInterfaceDetailData->DevicePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, 0, NULL);

                    if (hBattery != INVALID_HANDLE_VALUE) {
                        // Query the BatteryTag
                        ULONG batteryTag = 0;
                        DWORD bytesReturned = 0;
                        if (DeviceIoControl(hBattery,
                            IOCTL_BATTERY_QUERY_TAG,
                            NULL, 0,
                            &batteryTag, sizeof(batteryTag),
                            &bytesReturned, NULL) && batteryTag != 0) {

                            // Query battery information
                            BATTERY_QUERY_INFORMATION bqi = { 0 };
                            bqi.BatteryTag = batteryTag;
                            bqi.InformationLevel = BatteryInformation;

                            BATTERY_INFORMATION bi = { 0 };
                            if (DeviceIoControl(hBattery,
                                IOCTL_BATTERY_QUERY_INFORMATION,
                                &bqi, sizeof(bqi),
                                &bi, sizeof(bi),
                                &bytesReturned, NULL)) {

                                // Get battery health percentage
                                if (bi.DesignedCapacity > 0) {
                                    info.healthPercentage = (double)bi.FullChargedCapacity / bi.DesignedCapacity * 100.0;
                                    info.chargeCycles = bi.CycleCount;
                                    info.classification = ClassifyBatteryHealth(info.healthPercentage, info.chargeCycles, info.ageInYears);
                                    info.isValid = true;

                                    CloseHandle(hBattery);
                                    free(pDeviceInterfaceDetailData);
                                    SetupDiDestroyDeviceInfoList(hDevInfo);
                                    return info;
                                }
                            }
                        }
                        CloseHandle(hBattery);
                    }
                }
                free(pDeviceInterfaceDetailData);
            }
        }
        deviceIndex++;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return info;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    BatteryHealthInfo batteryInfo = EnumerateAndGetBatteryHealth();

    std::wstring message;
    std::wstring title;
    UINT iconType;

    if (batteryInfo.isValid) {
        std::wstringstream ss;
        ss << L"Battery Health: " << std::fixed << std::setprecision(1) << batteryInfo.healthPercentage << L"%\n";
        ss << L"Charge Cycles: " << batteryInfo.chargeCycles << L"\n";
        ss << L"Windows Install Date: " << batteryInfo.activationDate << L"\n";
        ss << L"System Age: " << std::fixed << std::setprecision(1) << batteryInfo.ageInYears << L" years\n";
        ss << L"Classification: " << batteryInfo.classification;

        message = ss.str();
        title = L"Battery Health Report";

        // Set icon based on classification
        if (batteryInfo.classification == L"Good") {
            iconType = MB_ICONINFORMATION;
        }
        else if (batteryInfo.classification == L"Medium") {
            iconType = MB_ICONWARNING;
        }
        else {
            iconType = MB_ICONEXCLAMATION;
        }
    }
    else {
        message = L"Unable to retrieve battery information.\nNo working battery found or access denied.";
        title = L"Battery Health - Error";
        iconType = MB_ICONERROR;
    }

    MessageBox(NULL, message.c_str(), title.c_str(), MB_OK | iconType);

    return 0;
}