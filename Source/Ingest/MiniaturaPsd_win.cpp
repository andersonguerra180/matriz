#include "MiniaturaPsd.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <comdef.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <JuceHeader.h>

#pragma comment(lib, "windowscodecs.lib")

bool gerarMiniaturaPsdNativa(const char* origemPath, const char* destinoPath,
                             int ladoMaximoPx, int* larguraOut, int* alturaOut) {
    if (!origemPath || !destinoPath || !larguraOut || !alturaOut) return false;

    // Inicializa COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);

    IWICImagingFactory* pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pFactory));
    if (FAILED(hr) || !pFactory) {
        if (needUninit) CoUninitialize();
        return false;
    }

    bool sucesso = false;
    juce::String wOrigemStr = juce::String::fromUTF8(origemPath);
    juce::String wDestStr = juce::String::fromUTF8(destinoPath);

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(wOrigemStr.toWideCharPointer(), nullptr,
                                             GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                             &pDecoder);
    if (SUCCEEDED(hr) && pDecoder) {
        UINT frameCount = 0;
        pDecoder->GetFrameCount(&frameCount);
        if (frameCount > 0) {
            IWICBitmapFrameDecode* pFrame = nullptr;
            hr = pDecoder->GetFrame(0, &pFrame);
            if (SUCCEEDED(hr) && pFrame) {
                UINT origW = 0, origH = 0;
                pFrame->GetSize(&origW, &origH);
                if (origW > 0 && origH > 0) {
                    double aspect = static_cast<double>(origW) / static_cast<double>(origH);
                    UINT targetW = origW;
                    UINT targetH = origH;
                    if (targetW > static_cast<UINT>(ladoMaximoPx) || targetH > static_cast<UINT>(ladoMaximoPx)) {
                        if (origW >= origH) {
                            targetW = static_cast<UINT>(ladoMaximoPx);
                            targetH = static_cast<UINT>(std::max(1.0, ladoMaximoPx / aspect));
                        } else {
                            targetH = static_cast<UINT>(ladoMaximoPx);
                            targetW = static_cast<UINT>(std::max(1.0, ladoMaximoPx * aspect));
                        }
                    }

                    IWICBitmapScaler* pScaler = nullptr;
                    hr = pFactory->CreateBitmapScaler(&pScaler);
                    if (SUCCEEDED(hr) && pScaler) {
                        hr = pScaler->Initialize(pFrame, targetW, targetH, WICBitmapInterpolationModeFant);
                        if (SUCCEEDED(hr)) {
                            IWICStream* pStream = nullptr;
                            hr = pFactory->CreateStream(&pStream);
                            if (SUCCEEDED(hr) && pStream) {
                                hr = pStream->InitializeFromFilename(wDestStr.toWideCharPointer(), GENERIC_WRITE);
                                if (SUCCEEDED(hr)) {
                                    IWICBitmapEncoder* pEncoder = nullptr;
                                    hr = pFactory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &pEncoder);
                                    if (SUCCEEDED(hr) && pEncoder) {
                                        hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
                                        if (SUCCEEDED(hr)) {
                                            IWICBitmapFrameEncode* pEncodeFrame = nullptr;
                                            IPropertyBag2* pPropertyBag = nullptr;
                                            hr = pEncoder->CreateNewFrame(&pEncodeFrame, &pPropertyBag);
                                            if (SUCCEEDED(hr) && pEncodeFrame) {
                                                PROPBAG2 optQuality = {};
                                                optQuality.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
                                                VARIANT varValue;
                                                VariantInit(&varValue);
                                                varValue.vt = VT_R4;
                                                varValue.fltVal = 0.85f;
                                                pPropertyBag->Write(1, &optQuality, &varValue);

                                                hr = pEncodeFrame->Initialize(pPropertyBag);
                                                if (SUCCEEDED(hr)) {
                                                    pEncodeFrame->SetSize(targetW, targetH);
                                                    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
                                                    pEncodeFrame->SetPixelFormat(&format);
                                                    hr = pEncodeFrame->WriteSource(pScaler, nullptr);
                                                    if (SUCCEEDED(hr)) {
                                                        pEncodeFrame->Commit();
                                                        pEncoder->Commit();
                                                        *larguraOut = static_cast<int>(targetW);
                                                        *alturaOut = static_cast<int>(targetH);
                                                        sucesso = true;
                                                    }
                                                }
                                                pEncodeFrame->Release();
                                            }
                                            if (pPropertyBag) pPropertyBag->Release();
                                        }
                                        pEncoder->Release();
                                    }
                                }
                                pStream->Release();
                            }
                        }
                        pScaler->Release();
                    }
                }
                pFrame->Release();
            }
        }
        pDecoder->Release();
    }

    pFactory->Release();
    if (needUninit) CoUninitialize();
    return sucesso;
}

#endif
