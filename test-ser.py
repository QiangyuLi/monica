import os
from datetime import date, timedelta

for doy in range(1, 365):
    sd = date(1991,9,23) + timedelta(days=doy)
    ed = date(1991,9,23) + timedelta(days=doy+3)
    os.system("_cmake_win64\Release\monica-run.exe -sd " + sd.isoformat() + " -ed " + ed.isoformat() + " installer\Hohenfinow2\sim-ser.json")



