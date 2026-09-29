/** @file PresetManager.h
 *  @brief Presets stored on SD card, read/parsed once at boot.
 *
 *  Most control values are floats in the UI [0,1] and are stored in the JSON as * 1000
 */
#pragma once
#include "FileStreamingManager.h"
#include "core_json.h"

#define PRE_VERSION "3"

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
        for(size_t slot = 0; slot < kMaxSlots; slot++)
        {
            for(int control = 0; control < kMaxControls; control++)
            {
                slotValues[slot][control] = defaults[control];
            }
        }
    }

    // The modes and banks aren't used in WAVE or TEMPO, but they are in TAPE
    static constexpr int kMaxModes = 2; // jammi, cubbi
    static constexpr int kMaxBanks = 3; // a, b, c
    static constexpr int kMaxSlots = 14; // 1, 2, 3, etc.
    static constexpr int kMaxControls = 14;
    // 0 pitch, 1 cycle, 2 table, 3 attack, 4 pitch-LFO amt, 5 release,
    // 6 filter-LFO amt, 7 cutoff, 8 pitch-LFO rate, 9 delay wet, 10 resonance,
    // 11 filter-LFO rate, 12 delay time, 13 LFO on/off bitfield (pitch=1, filter=2)
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
        for(int slot = 0; slot < kMaxSlots; slot++)
        {
            sprintf(append,"[");
            StrAppend(buffer, append);

            for(int ctrl = 0; ctrl < kMaxControls; ctrl++)
            {
                if (ctrl != 1 && ctrl != 2 && ctrl != 13) {
                    sprintf(append,"%d,", int(slotValues[slot][ctrl] * 1000));
                }
                else {
                    sprintf(append,"%d,", int(slotValues[slot][ctrl]));
                }
                
                StrAppend(buffer, append);
            }

            char valid[6];
            if(slotValid[slot])
                strcpy(valid, "true");
            else
                strcpy(valid, "false");

            sprintf(append,"%s],", valid);
            StrAppend(buffer, append);
        }
        // buffer[strlen(buffer) - 1] = '\0';

        strcpy(append, PRE_VERSION);
        StrAppend(buffer, append);

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

            /** Parse version number. No version number == version 1*/
            sprintf(query, "[2]");
            json_res = JSON_Search(
                buffer, len, query, strlen(query), &value, &value_len);

            // Backward compatible with presets saved by older firmware. If this lookup
            // doesn't succeed, treat the file as the original V1 format with only 7 controls
            uint8_t numcontrols = kMaxControls;
            if(json_res != JSONSuccess) // V1
                numcontrols = 7;

            /** Get module presets */
                    for(int slot = 0; slot < kMaxSlots; slot++)
                    {
                        // valid?
                        sprintf(query, "[%d][%d]", slot, numcontrols);
                        json_res = JSON_Search(
                            buffer, len, query, strlen(query), &value, &value_len);
                        
                        char save = value[4];
                        value[4] = '\0';
                        bool valid = strcmp(value, "true") == 0;
                        value[4] = save;

                        if(json_res == JSONSuccess && valid)
                        {
                            for(int ctrl = 0; ctrl < numcontrols; ctrl++)
                            {
                                /** and now check for the value */
                                sprintf(query, "[%d][%d]", slot, ctrl);

                                json_res = JSON_Search(
                                    buffer, len, query, strlen(query), &value, &value_len);

                                if(json_res == JSONSuccess)
                                {
                                    char tmp[16];
                                    size_t n = value_len < sizeof(tmp)-1 ? value_len : sizeof(tmp)-1;
                                    memcpy(tmp, value, n);
                                    tmp[n] = '\0';

                                    if (ctrl == 0) {
                                        int iv = atoi(tmp);
                                        slotValues[slot][ctrl] = nextafterf(iv * 0.001f, -INFINITY);
                                    }
                                    else if (ctrl != 1 && ctrl != 2 && ctrl != 13) {
                                        slotValues[slot][ctrl] = .001f * atof(tmp);
                                    }
                                    else {
                                        slotValues[slot][ctrl] = atof(tmp);
                                    }
                                }
                            }
                            slotValid[slot] = true;
                        }
                        else
                        {
                            slotValid[slot] = false;
                        }
            }

            ret = Result::OK;
        }

        WriteWholeFile(buffer, size);

        return ret;
    }

    bool IsValid(size_t slot)
    {
        slot -= 1;
        if(slot >= kMaxSlots)
            return false;

        return slotValid[slot];
    }

    /** Returns the string value of a given key, or NULL */
    float GetValue(size_t mode, size_t bank, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot > kMaxSlots || control >= kMaxControls)
            return 0xff;
        
        if(!slotValid[slot])
            return 0xff;

        return slotValues[slot][control];
    }

    void SetValue(float value, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot > kMaxSlots || control >= kMaxControls)
            return;

        else
        {
            slotValues[slot][control] = value;
        }
    }

    void Invalidate(uint8_t slot)
    {
        slot -= 1;
        if(slot >= kMaxSlots)
            return;

        slotValid[slot] = false;

        updated = true;
    }

    void Save(size_t slot)
    {
        updated = true;
        slotValid[slot - 1] = true;
    }

    // There is a lot of weird math converting the key number to slot number
    // In some cases its zero indexed, in others its 1 indexed. This should
    // probably all be unified.
    void Copy(uint8_t mode_src, uint8_t bank_src, uint8_t slot_src, 
              uint8_t mode_trg, uint8_t bank_trg, uint8_t slot_trg)
    {
        slot_trg -= 1;
        slot_src -= 1;
        if(slot_src > kMaxSlots)
            return;

        if(slot_trg > kMaxSlots)
            return;

        float* src = slotValues[slot_src];
        float* dest = slotValues[slot_trg];
        for(size_t i = 0; i < kMaxControls; i++)
        {
            dest[i] = src[i];
        }

        bool valid = slotValid[slot_src];
        if(slot_trg == 14)
            chompi_valid = valid;
        else
            slotValid[slot_trg] = valid;
    
        updated = true;
    }



    float values[kMaxModes][kMaxBanks][kMaxSlots][kMaxControls];
    bool values_valid[kMaxModes][kMaxBanks][kMaxSlots];

    float slotValues[15][kMaxControls];
    bool slotValid[15];

    float chompi_value[kMaxControls]; // only store the chompi settings in memory
    bool chompi_valid;

    private:
        bool updated;
};
} // namespace chompi