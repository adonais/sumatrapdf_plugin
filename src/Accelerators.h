/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

HACCEL* CreateSumatraAcceleratorTable();
HACCEL* GetSafeAcceleratorTable();
bool GetAccelByCmd(int cmdId, ACCEL& accelOut);
