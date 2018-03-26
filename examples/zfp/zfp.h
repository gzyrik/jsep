int Zfp_Open(const char *filePath, void *userdata,
    void (*pfnVideo)(void *userdata, int playedMs, int width, int height, void *i420Buf),
    void (*pfnAudio)(void *userdata, int playedMs, int samplingHz, int channels, void *pcmBuf, int len));
void Zfp_Close(const char *filePath);
//- 'LengthMs', media time length in milliseconds
//- 'PlayedMs', played time in milliseconds
const char* Zfp_GetInfo(const char *filePath, const char *name, int* dft);
int Zfp_ReadAudio(const char *filePath, int samplingHz, int channcels, void *pcmBuf, int len);
int Zfp_Pause(const char *filePath);
int Zfp_Resume(const char *filePath);
int Zfp_Seek(const char *filePath, int offsetMs, int origin); //¿‡À∆ fseek
