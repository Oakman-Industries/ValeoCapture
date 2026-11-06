// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// UTF-8 JSON values; caller releases with VCFree. No object pointer crosses XPC.
char* VCDiscover(void);
char* VCProbe(unsigned long long registry_id);
char* VCInspectCamera(unsigned long long registry_id);
char* VCReserveCapture(unsigned int iso);
char* VCRequestSoftwareISO(unsigned int iso);
char* VCCaptureState(void);
char* VCStopCapture(void);
char* VCRunCapture(unsigned long long registry_id, const char* folder,
    unsigned int iso, unsigned int orientation, unsigned int white_level,
    const double* xyz_to_camera, const double* as_shot_neutral,
    const char* expected_body_serial, const void* profile, unsigned long long profile_size,
    int save_tiff);
char* VCValidateProfile(const char* path);
char* VCValidateProfileData(const void* data, unsigned long long size);
char* VCWriteSyntheticDNG(const char* new_path);
void VCFree(void*);
#ifdef __cplusplus
}
#endif
