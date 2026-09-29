#pragma once
#include "daisy.h"
#include "SampleManager.h"
#include "daisysp.h"
#include "DJFilter.h"
#include "EnvFollower.h"
#include "limiter.h"
#include <cmath>

#define NUM_VOICES 8

struct KeyRequest
{
    enum class Type
    {
        START,
        STOP,
        DUMMY,
    };

    enum class Source
    {
        USER,
        SEQUENCER,
    };

    Type type_;
    float transpose_nn_;
    int key_;
    float vel_;
    Source src_;

    /** constructor for full request data */
    KeyRequest(Type type,
                float transpose_nn,
                int key,
                float vel,
                Source src = Source::USER
              )
        : type_(type),
            transpose_nn_(transpose_nn),
            key_(key),
            vel_(vel),
            src_(src)
    {
    }

    /** Empty, invalid request */
    KeyRequest()
        : type_(Type::DUMMY),
            transpose_nn_(0.f),
            key_(0),
            vel_(127.f),
            src_(Source::USER)
    {
    }
};

class BaseEngine {
    public:
    BaseEngine() {};
    virtual ~BaseEngine() {};

    virtual void Init(sampleManager *sample_manager, float sample_rate) = 0;
    virtual void Process(const float *const *in, float **out, size_t size) = 0;
    virtual void Prepare() = 0;
    virtual void setGlobalPitchFree(float val) = 0;
    virtual float setGlobalPitchQuantized(int16_t turns, float enc_pos) = 0;
    virtual void resetGlobalPitchQuant() = 0;
    virtual bool isKeyPlaying(size_t key) = 0;
    virtual void setSample(size_t slot) = 0;
    virtual void saveFileToSlot(size_t slot) = 0;
    virtual void deleteSlot(size_t slot) = 0;
    virtual void copySlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) = 0;
    virtual bool isValidSample(size_t slot) = 0;
    virtual void setAttack(float amount) = 0;
    virtual void setRelease(float amount) = 0;
    virtual void setStartPoint(float val) = 0;
    virtual void setEndPoint(float val) = 0;
    virtual void setMasterCutoff(float amount) = 0;
    virtual void setMasterRes(float amount) = 0;
    virtual void setPan(float val) = 0;
    virtual void setSampleReducer(float val) = 0;
    virtual void setSampleVolume(float val) = 0;
    virtual uint8_t getVoiceSlot() = 0;
    virtual void setLoop(float amount) = 0;
    virtual void setSustain(float amount) = 0;
    virtual void handleRetrigEnv(bool on, size_t key) = 0;
    virtual void getActiveKeys(uint16_t *activeKeys, float *nns) = 0;
    virtual float chokeVoice(size_t voice) = 0;
    virtual void setLastSlice(bool t) = 0;

    FIFO<KeyRequest, 64> request_fifo;

};
