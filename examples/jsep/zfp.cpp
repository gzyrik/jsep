int Zfp_Open(const char *filePath, void *userdata,
    void (*pfnVideo)(void *userdata, int playedMs, int width, int height, void *buf);
    void (*pfnAudio)(void *userdata, int playedMs, int samplingHz, int channels, void *buf, int len));
void Zfp_Close(const char *filePath);
const char* Zfp_GetInfoStr(const char *filePath, const char *name);
int Zfp_GetInfoInt(const char *filePath, const char *name, int dft);
int Zfp_ReadAudio(const char *filePath, int samplingHz, int channcels, void *buf, int len);
int Zfp_Pause(const char *filePath);
int Zfp_Resume(const char *filePath);
int Zfp_Seek(const char *filePath, int offsetMs, int origin);
