#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#pragma comment(lib, "Ole32.lib")

class AudioCapture {
public:
    AudioCapture() {}
    ~AudioCapture() { Stop(); }

    void Start() {
        if (running) return;
        running = true;
        captureThread = std::thread(&AudioCapture::Loop, this);
    }

    void Stop() {
        running = false;
        if (captureThread.joinable()) captureThread.join();
    }

    void GetAudioData(std::vector<float> &outBuffer) {
        std::lock_guard<std::mutex> lock(dataMutex);
        outBuffer = currentBuffer;
    }

private:
    std::atomic<bool> running = false;
    std::thread captureThread;
    std::mutex dataMutex;
    std::vector<float> currentBuffer;
    const int FFT_SIZE = 512;

    void Loop() {
        HRESULT hr;
        IMMDeviceEnumerator *pEnumerator = NULL;
        IMMDevice *pDevice = NULL;
        IAudioClient *pAudioClient = NULL;
        IAudioCaptureClient *pCaptureClient = NULL;
        WAVEFORMATEX *pwfx = NULL;

        CoInitialize(NULL);

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator));
        if (FAILED(hr)) goto Exit;

        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
        if (FAILED(hr)) goto Exit;

        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&pAudioClient);
        if (FAILED(hr)) goto Exit;

        hr = pAudioClient->GetMixFormat(&pwfx);
        if (FAILED(hr)) goto Exit;

        // Force Loopback
        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pwfx, NULL);
        if (FAILED(hr)) goto Exit;

        hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void **)&pCaptureClient);
        if (FAILED(hr)) goto Exit;

        hr = pAudioClient->Start();
        if (FAILED(hr)) goto Exit;

        while (running) {
            UINT32 packetLength = 0;
            hr = pCaptureClient->GetNextPacketSize(&packetLength);

            while (packetLength != 0) {
                BYTE *pData;
                UINT32 numFramesAvailable;
                DWORD flags;

                hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
                if (FAILED(hr)) break;

                if (numFramesAvailable > 0) {
                    std::vector<float> tempBuf;
                    tempBuf.resize(FFT_SIZE, 0.0f);

                    // --- FORMAT DETECTION & NORMALIZATION ---
                    // This ensures data is ALWAYS between -1.0 and 1.0
                    int channels = pwfx->nChannels;
                    int bitsPerSample = pwfx->wBitsPerSample;
                    int bytesPerFrame = pwfx->nBlockAlign;

                    for (UINT32 i = 0; i < numFramesAvailable && i < FFT_SIZE; i++) {
                        float sample = 0.0f;
                        BYTE *framePtr = pData + (i * bytesPerFrame);

                        if (bitsPerSample == 32) {
                            // 32-bit Float (Most common)
                            float *pFloat = (float *)framePtr;
                            // Downmix stereo to mono
                            if (channels >= 2) sample = (pFloat[0] + pFloat[1]) * 0.5f;
                            else sample = pFloat[0];
                        }
                        else if (bitsPerSample == 16) {
                            // 16-bit Integer (Scale down from 32768)
                            short *pShort = (short *)framePtr;
                            if (channels >= 2) sample = ((float)pShort[0] + (float)pShort[1]) * 0.5f;
                            else sample = (float)pShort[0];

                            sample /= 32768.0f; // Normalize!
                        }
                        else if (bitsPerSample == 24) {
                            // 24-bit Integer (Scale down from 8.3 million)
                            int32_t val = (framePtr[2] << 24) | (framePtr[1] << 16) | (framePtr[0] << 8);
                            sample = (float)(val >> 8) / 8388608.0f; // Normalize!
                        }

                        tempBuf[i] = sample;
                    }
                    // ---------------------------------------

                    {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        currentBuffer = tempBuf;
                    }
                }

                hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
                hr = pCaptureClient->GetNextPacketSize(&packetLength);
            }
            Sleep(10);
        }

        pAudioClient->Stop();

    Exit:
        if (pwfx) CoTaskMemFree(pwfx);
        if (pCaptureClient) pCaptureClient->Release();
        if (pAudioClient) pAudioClient->Release();
        if (pDevice) pDevice->Release();
        if (pEnumerator) pEnumerator->Release();
        CoUninitialize();
    }
};