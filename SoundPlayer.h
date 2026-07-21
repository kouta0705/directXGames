#pragma once
#include <xaudio2.h>
#include <wrl.h>
#include <string>
#include <fstream>
#include <vector>
#include <cassert>
#pragma comment(lib, "xaudio2.lib")

using Microsoft::WRL::ComPtr;

// 音声再生を担当するクラス
class SoundPlayer {
public:
    SoundPlayer() = default;
    ~SoundPlayer() { Finalize(); }

    // XAudio2エンジンの初期化（アプリ起動時に1回だけ呼ぶ）
    bool Initialize() {
        HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr)) return false;

        hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
        if (FAILED(hr)) return false;

        return true;
    }

    // wavファイルを読み込む（.wavのPCMデータ限定・簡易パーサ）
    bool LoadWave(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        // RIFFヘッダ
        RiffHeader riff{};
        file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
        assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
        assert(strncmp(riff.type, "WAVE", 4) == 0);

        // チャンクを順に読んで fmt / data を探す
        ChunkHeader chunkHeader{};
        FormatChunk format{};
        std::vector<BYTE> data;

        while (file.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader))) {
            if (strncmp(chunkHeader.id, "fmt ", 4) == 0) {
                file.read(reinterpret_cast<char*>(&format.fmt), chunkHeader.size);
            }
            else if (strncmp(chunkHeader.id, "data", 4) == 0) {
                data.resize(chunkHeader.size);
                file.read(reinterpret_cast<char*>(data.data()), chunkHeader.size);
            }
            else {
                // 未対応チャンクはスキップ
                file.seekg(chunkHeader.size, std::ios_base::cur);
            }
        }

        waveFormat_ = format.fmt;
        bufferData_ = std::move(data);
        return true;
    }

    // 読み込んだ音を再生する（再生ごとにSourceVoiceを作り直す簡易実装）
    void Play(bool loop = false) {
        if (bufferData_.empty()) return;

        IXAudio2SourceVoice* sourceVoice = nullptr;
        HRESULT hr = xAudio2_->CreateSourceVoice(&sourceVoice, &waveFormat_);
        assert(SUCCEEDED(hr));

        XAUDIO2_BUFFER buf{};
        buf.pAudioData = bufferData_.data();
        buf.AudioBytes = static_cast<UINT32>(bufferData_.size());
        buf.Flags = XAUDIO2_END_OF_STREAM;
        if (loop) buf.LoopCount = XAUDIO2_LOOP_INFINITE;

        hr = sourceVoice->SubmitSourceBuffer(&buf);
        assert(SUCCEEDED(hr));
        hr = sourceVoice->Start();
        assert(SUCCEEDED(hr));

        // 再生済みのvoiceは自動破棄されないので保持しておく
        playingVoices_.push_back(sourceVoice);
        CleanupFinishedVoices();
    }

    void Finalize() {
        for (auto* voice : playingVoices_) {
            voice->Stop();
            voice->DestroyVoice();
        }
        playingVoices_.clear();

        if (masterVoice_) {
            masterVoice_->DestroyVoice();
            masterVoice_ = nullptr;
        }
        xAudio2_.Reset();
    }

private:
    struct ChunkHeader {
        char id[4];
        uint32_t size;
    };
    struct RiffHeader {
        ChunkHeader chunk;
        char type[4];
    };
    struct FormatChunk {
        WAVEFORMATEX fmt;
    };

    void CleanupFinishedVoices() {
        // 再生が終わったvoiceを破棄する（毎回全チェックは重いので簡易的に）
        for (auto it = playingVoices_.begin(); it != playingVoices_.end();) {
            XAUDIO2_VOICE_STATE state{};
            (*it)->GetState(&state);
            if (state.BuffersQueued == 0) {
                (*it)->Stop();
                (*it)->DestroyVoice();
                it = playingVoices_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;
    WAVEFORMATEX waveFormat_{};
    std::vector<BYTE> bufferData_;
    std::vector<IXAudio2SourceVoice*> playingVoices_;
};