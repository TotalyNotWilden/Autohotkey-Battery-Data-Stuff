#Requires AutoHotkey v2.0
; Hotkey: Shift + Ctrl + Alt + M
+^!m::ShowBatteryInfo()

ShowBatteryInfo() {
    info := Map()

    ; ---- System Power Status ----
    sps := GetSystemPowerStatus()
    if sps {
        info["AC Line"] := (sps["ACLineStatus"]=1) ? "Plugged in (AC)" : (sps["ACLineStatus"]=0 ? "On battery" : "Unknown")
        info["Battery %"] := (sps["BatteryLifePercent"] >= 0 && sps["BatteryLifePercent"] <= 100)
            ? sps["BatteryLifePercent"] . "%"
            : "N/A"
        info["Estimated Time Left"] := (sps["BatteryLifeTime"] >= 0)
            ? SecsToHMS(sps["BatteryLifeTime"])
            : "N/A"
        info["Estimated Full Lifetime"] := (sps["BatteryFullLifeTime"] > 0)
            ? SecsToHMS(sps["BatteryFullLifeTime"])
            : "N/A"
        info["Battery Flags"] := BatteryFlagsToText(sps["BatteryFlag"])
    }

    ; ---- WMI Battery Details ----
    w := GetWmiBatteryData()
    if w.Has("FullChargedCapacity") && w.Has("DesignedCapacity") && w["FullChargedCapacity"]>0 && w["DesignedCapacity"]>0 {
        health := Round((w["FullChargedCapacity"] / w["DesignedCapacity"]) * 100, 1)
        wear := Round(100 - health, 1)
        info["Battery Health"] := health . "% (wear " . wear . "%)"
    } else if w.Has("FullChargedCapacity") || w.Has("DesignedCapacity") {
        info["Battery Health"] := "Not enough data"
    }

    if w.Has("RemainingCapacity") && w.Has("FullChargedCapacity") && w["FullChargedCapacity"]>0 {
        pct := Round((w["RemainingCapacity"] / w["FullChargedCapacity"]) * 100, 0)
        info["Battery % (WMI)"] := pct . "%"
    }

    if w.Has("CycleCount")
        info["Cycle Count"] := w["CycleCount"]

    if w.Has("Voltage") && w["Voltage"]>0
        info["Voltage"] := Round(w["Voltage"] / 1000, 2) . " V"

    if w.Has("Rate") && w["Rate"] != ""
        info["Rate"] := w["Rate"] . " mW (negative = discharging)"

    if w.Has("Chemistry") && w["Chemistry"] != ""
        info["Chemistry"] := w["Chemistry"]

    if w.Has("DeviceName") && w["DeviceName"] != ""
        info["Battery Name"] := w["DeviceName"]

    if w.Has("SerialNumber") && w["SerialNumber"] != ""
        info["Serial"] := w["SerialNumber"]

    if w.Has("ManufactureDate") && w["ManufactureDate"] != ""
        info["Manufactured"] := w["ManufactureDate"]

    if w.Has("DesignedCapacity") && w["DesignedCapacity"]>0
        info["Design Capacity"] := w["DesignedCapacity"] . " mWh"

    if w.Has("FullChargedCapacity") && w["FullChargedCapacity"]>0
        info["Full Charge Capacity"] := w["FullChargedCapacity"] . " mWh"

    if w.Has("RemainingCapacity") && w["RemainingCapacity"]>=0
        info["Remaining Capacity"] := w["RemainingCapacity"] . " mWh"

    ; Build message
    text := "🔋 Battery Info`n--------------------------`n"
    for k, v in info
        text .= k . ": " . v . "`n"

    MsgBox(text, "Battery Status", "OK Iconi")
}

GetSystemPowerStatus() {
    buf := Buffer(12, 0)
    ok := DllCall("GetSystemPowerStatus", "ptr", buf.Ptr, "int")
    if !ok
        return 0
    sps := Map()
    sps["ACLineStatus"] := NumGet(buf, 0, "UChar")
    sps["BatteryFlag"] := NumGet(buf, 1, "UChar")
    sps["BatteryLifePercent"] := NumGet(buf, 2, "UChar")
    sps["SystemStatusFlag"] := NumGet(buf, 3, "UChar")
    sps["BatteryLifeTime"] := NumGet(buf, 4, "UInt")
    sps["BatteryFullLifeTime"] := NumGet(buf, 8, "UInt")
    return sps
}

BatteryFlagsToText(flag) {
    if flag = 255
        return "Unknown"
    parts := []
    if (flag & 1)
        parts.Push("High")
    if (flag & 2)
        parts.Push("Low")
    if (flag & 4)
        parts.Push("Critical")
    if (flag & 8)
        parts.Push("Charging")
    if (flag & 128)
        parts.Push("No battery")
    return parts.Length ? StrJoin(parts, ", ") : "Normal"
}

GetWmiBatteryData() {
    data := Map()
    try {
        wmi := ComObjGet("winmgmts:\\.\root\WMI")

        for f in wmi.ExecQuery("SELECT * FROM BatteryFullChargedCapacity") {
            data["FullChargedCapacity"] := SafeInt(f.FullChargedCapacity)
            break
        }
        for s in wmi.ExecQuery("SELECT * FROM BatteryStaticData") {
            data["DesignedCapacity"] := SafeInt(s.DesignedCapacity)
            data["Chemistry"] := TryChemistry(s.Chemistry)
            data["ManufactureDate"] := TryMfgDate(s.ManufactureDate)
            data["DeviceName"] := s.DeviceName ? s.DeviceName : ""
            data["SerialNumber"] := s.SerialNumber ? s.SerialNumber : ""
            break
        }
        for b in wmi.ExecQuery("SELECT * FROM BatteryStatus") {
            data["RemainingCapacity"] := SafeInt(b.RemainingCapacity)
            rate := ""
            try {
                rate := b.DischargeRate
            }
            if (rate = "" || rate = 0) {
                try {
                    rate := b.ChargeRate
                }
            }
            data["Rate"] := rate
            try {
                data["Voltage"] := SafeInt(b.Voltage)
            }
            break
        }
        try {
            for c in wmi.ExecQuery("SELECT * FROM BatteryCycleCount") {
                data["CycleCount"] := SafeInt(c.CycleCount)
                break
            }
        }
    } catch as e {
        ; Ignore errors if WMI class not available
    }
    return data
}

SafeInt(v) {
    try {
        return Integer(v)
    } catch {
        try {
            return Round(v)
        } catch {
            return 0
        }
    }
}

TryChemistry(raw) {
    try {
        code := Integer(raw)
    } catch {
        return raw ? raw : ""
    }
    chem := Map(
        1, "Other",
        2, "Unknown",
        3, "Lead Acid",
        4, "NiCd",
        5, "NiMH",
        6, "Li-ion",
        7, "Zinc-Air",
        8, "Li-Po"
    )
    return chem.Has(code) ? chem[code] : "Unknown"
}

TryMfgDate(raw) {
    if raw = "" || raw = 0
        return ""
    try {
        s := "" . raw
        if RegExMatch(s, "^\d{8}$") {
            y := SubStr(s, 1, 4), m := SubStr(s, 5, 2), d := SubStr(s, 7, 2)
            return y "-" m "-" d
        }
        v := Integer(raw)
        y := (v >> 9) + 1980
        m := (v >> 5) & 0xF
        d := v & 0x1F
        if (y>=1980 && m>=1 && m<=12 && d>=1 && d<=31)
            return Format("{:04}-{:02}-{:02}", y, m, d)
    } catch {
    }
    return raw
}

SecsToHMS(s) {
    if (s < 0)
        return "N/A"
    h := Floor(s / 3600)
    m := Floor((s - h*3600) / 60)
    return Format("{:02}:{:02} (hh:mm)", h, m)
}

StrJoin(arr, sep := ", ") {
    out := ""
    for i, v in arr
        out .= (i>1 ? sep : "") . v
    return out
}
