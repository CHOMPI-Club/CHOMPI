#pragma once
#include "FileStreamingManager.h"
#include "core_json.h"

namespace chompi {
class PresetManager 
{
    public:
    PresetManager() {}
    ~PresetManager() {}


    void Init(float* defaults)
    {
        updated = true;

        // fill in defaults. probably a more efficient way to do this with memcpy
        for (size_t mode = 0; mode < kMaxModes; ++mode) {
            for(size_t slot = 0; slot < kMaxSlots; slot++)
            {
                for(int control = 0; control < kMaxControls; control++)
                {
                    presetValues[mode][slot][control] = defaults[control];
                }
            }
        }

        for (size_t i = 0; i < kMaxControls; ++i) {
            preset_defaults[i] = presetValues[0][0][i];
        }
    }

    static constexpr int kMaxModes = 2; // chromatic, slice
    static constexpr int kMaxSlots = 14; // 1, 2, 3, etc.
    static constexpr int kMaxControls = 11;

    enum class Result
    {
        OK,
        ERR_BUFF_OVERFLOW,
        ERR_INVALID_JSON,
        ERR_GENERIC,
    };

    void StrAppend(char* buffer, char* append)
    {
        size_t len = strlen(buffer);
        size_t app_len = strlen(append);

        for(size_t i = 0; i < app_len; i++)
        {
            buffer[len + i] = append[i];
        }
        buffer[len + app_len] = '\0';
    }

    Result WriteWholeFile(char* buffer, size_t size)
    {
        if(!updated)
            return Result::ERR_GENERIC;

        updated = false;

        std::fill_n(buffer, size, '\0');
        strcpy(buffer, "[");
        char append[16];
        for (size_t mode = 0; mode < kMaxModes; ++mode) {

            sprintf(append,"[");
            StrAppend(buffer, append);    

            for(int slot = 0; slot < kMaxSlots; slot++)
            {
                sprintf(append,"[");
                StrAppend(buffer, append);

                for(int ctrl = 0; ctrl < kMaxControls; ctrl++)
                {
                    sprintf(append,"%d,", int(presetValues[mode][slot][ctrl] * 1000));
                    if (ctrl == 10) {
                        sprintf(append,"%d", int(presetValues[mode][slot][ctrl] * 1000));
                    }
                    StrAppend(buffer, append);
                }

                sprintf(append,"],");
                StrAppend(buffer, append);
            }

            buffer[strlen(buffer) - 1] = '\0';
            sprintf(append,"],");
            if (mode == 1) {
                sprintf(append,"]");
            }
            StrAppend(buffer, append);

        }

        strcpy(append, "]"),
        StrAppend(buffer, append);

        return Result::OK;
    }

    /** loads the JSON file, storing in name/value pairs for all keys */
    Result Parse(char* buffer, size_t size)
    {
        updated = true;
        JSONStatus_t json_res;

        Result ret = Result::ERR_INVALID_JSON;
        
        size_t len = strlen(buffer);
        json_res = JSON_Validate(buffer, len);
        if(json_res == JSONSuccess)
        {
            char   query[51];
            char*  value;
            size_t value_len;

            /** Get module presets */
            for (int mode = 0; mode < kMaxModes; ++mode)
            {
                for(int slot = 0; slot < kMaxSlots; slot++)
                {
                    for(int ctrl = 0; ctrl < kMaxControls; ctrl++)
                    {
                        /** and now check for the value */
                        sprintf(query, "[%d][%d][%d]", mode, slot, ctrl);

                        json_res = JSON_Search(buffer, len, query, strlen(query), &value, &value_len);

                        if(json_res == JSONSuccess)
                        {
                            char tmp[16];
                            size_t n = value_len < sizeof(tmp)-1 ? value_len : sizeof(tmp)-1;
                            memcpy(tmp, value, n);
                            tmp[n] = '\0';

                            float v;

                            if (ctrl == 0) {
                                int iv = atoi(tmp);
                                v = nextafterf(iv * 0.001f, -INFINITY);
                            }
                            else {
                                v = .001f * atof(value);
                            }

                            presetValues[mode][slot][ctrl] = v;
                        }
                    }
                }
            }

            ret = Result::OK;
        }

        WriteWholeFile(buffer, size);

        return ret;
    }

    /** Returns the string value of a given key, or NULL */
    float GetValue(size_t mode, size_t slot, size_t control)
    {
        if(slot > kMaxSlots || control >= kMaxControls)
            return 0xff;

        return presetValues[mode][slot][control];
    }

    void SetValue(float value, size_t mode, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot > kMaxSlots || control >= kMaxControls)
            return;

        else
        {
            presetValues[mode][slot][control] = value;
        }
    }

    float getDefault(size_t control) {
        return preset_defaults[control];
    }

    void Save(size_t mode, size_t slot)
    {
        updated = true;
    }

    float presetValues[kMaxModes][kMaxSlots][kMaxControls];

    private:
    bool updated;
    float preset_defaults[kMaxControls];
};
} // namespace chompi