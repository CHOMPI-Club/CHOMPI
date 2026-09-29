#pragma once
#include "FileStreamingManager.h"
#include "core_json.h"

#define PRE_VERSION "2"

namespace chompi {
class PresetManager 
{
    public:
    PresetManager() {}
    ~PresetManager() {}


    void Init(const float* defaults)
    {
        updated = true;
        defaults_ = defaults;

        // fill in defaults. probably a more efficient way to do this with memcpy
        for(size_t mode = 0; mode < kMaxModes; mode++)
        {
            for(size_t bank = 0; bank < kMaxBanks; bank++)
            {
                for(size_t slot = 0; slot < kMaxSlots; slot++)
                {
                    for(int control = 0; control < kMaxControls; control++)
                    {
                        if(slot == 14)
                            chompi_value[control] = defaults[control];
                        else
                            values[mode][bank][slot][control] = defaults[control];
                    }
                }
            }
        }
    }

    static constexpr int kMaxModes = 2; // jammi, cubbi
    static constexpr int kMaxBanks = 5; // a, b, c, d, e
    static constexpr int kMaxSlots = 14; // 1, 2, 3, etc.
    static constexpr int kMaxControls = 9; // pitch, start, end, att, decay, autoloop, sustainactive, gain, pan

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
        char append[32];
        for(int mode = 0; mode < kMaxModes; mode++)
        {
            sprintf(append,"[");
            StrAppend(buffer, append);

            for(int bank = 0; bank < kMaxBanks; bank++)
            {
                sprintf(append,"[");
                StrAppend(buffer, append);

                for(int slot = 0; slot < kMaxSlots; slot++)
                {
                    sprintf(append,"[");
                    StrAppend(buffer, append);

                    for(int ctrl = 0; ctrl < kMaxControls; ctrl++)
                    {
                        sprintf(append,"%d,", int(values[mode][bank][slot][ctrl] * 1000));
                        StrAppend(buffer, append);
                    }


                    char valid[6];
                    if(values_valid[mode][bank][slot])
                        strcpy(valid, "true");
                    else
                        strcpy(valid, "false");

                    sprintf(append,"%s],", valid);
                    StrAppend(buffer, append);
                }

                buffer[strlen(buffer) - 1] = '\0';
                sprintf(append,"],");
                StrAppend(buffer, append);
            }

            buffer[strlen(buffer) - 1] = '\0';
            sprintf(append,"],");
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

            uint8_t numcontrols = kMaxControls;
            if(json_res != JSONSuccess) // V1
                numcontrols = 7;

            /** Get module presets */
            for(int mode = 0; mode < kMaxModes; mode++){
                for(int bank = 0; bank < kMaxBanks; bank++)
                {
                    for(int slot = 0; slot < kMaxSlots; slot++)
                    {
                        // valid?
                        sprintf(query, "[%d][%d][%d][%d]", mode, bank, slot, numcontrols);
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
                                sprintf(query, "[%d][%d][%d][%d]",mode, bank, slot, ctrl);

                                json_res = JSON_Search(
                                    buffer, len, query, strlen(query), &value, &value_len);

                                if(json_res == JSONSuccess)
                                {
                                    values[mode][bank][slot][ctrl] = .001f * atof(value);
                                }
                            }
                            values_valid[mode][bank][slot] = true;
                        }
                        else
                        {
                            values_valid[mode][bank][slot] = false;
                        }
                    }
                }
            }

            ret = Result::OK;
        }

        WriteWholeFile(buffer, size);

        return ret;
    }

    bool IsValid(size_t mode, size_t bank, size_t slot)
    {
        slot -= 1;
        if(slot > kMaxSlots || mode >= kMaxModes || bank >= kMaxBanks)
            return false;

        if (slot == 14)
            return chompi_valid;

        return values_valid[mode][bank][slot];
    }

    /** Returns the string value of a given key, or NULL */
    float GetValue(size_t mode, size_t bank, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot > kMaxSlots || mode >= kMaxModes || bank >= kMaxBanks || control >= kMaxControls)
            return 0xff;
        
        if(!values_valid[mode][bank][slot] && slot != 14)
            return 0xff;
        
        if(!chompi_valid && slot == 14)
            return 0xff;

        if(slot == 14)
            return chompi_value[control];

        return values[mode][bank][slot][control];
    }

    void SetValue(float value, size_t mode, size_t bank, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot > kMaxSlots || mode >= kMaxModes || bank >= kMaxBanks || control >= kMaxControls)
            return;

        if(slot == 14)
        {
            chompi_value[control] = value;
            chompi_valid = true;
        }
        else
        {
            updated = true;
            values[mode][bank][slot][control] = value;
            values_valid[mode][bank][slot] = true;
        }
    }

    void Invalidate(uint8_t mode, uint8_t bank, uint8_t slot)
    {
        slot -= 1;
        if(slot >= kMaxSlots || mode >= kMaxModes || bank >= kMaxBanks)
            return;

        values_valid[mode][bank][slot] = false;

        updated = true;
    }

    void Save(uint8_t mode, uint8_t bank, uint8_t slot)
    {
        slot -= 1;
        if(slot >= kMaxSlots || mode >= kMaxModes || bank >= kMaxBanks)
            return;

        for(size_t i = 0; i < kMaxControls; i++)
        {
            values[mode][bank][slot][i] = chompi_value[i];
        }
        values_valid[mode][bank][slot] = chompi_valid;

        updated = true;
    }

    void Copy(uint8_t mode_src, uint8_t bank_src, uint8_t slot_src, 
              uint8_t mode_trg, uint8_t bank_trg, uint8_t slot_trg)
    {
        slot_trg -= 1;
        slot_src -= 1;
        if(slot_src > kMaxSlots || mode_src >= kMaxModes || bank_src >= kMaxBanks)
            return;

        if(slot_trg > kMaxSlots || mode_trg >= kMaxModes || bank_trg >= kMaxBanks)
            return;

        float* src = slot_src == 14 ? chompi_value : values[mode_src][bank_src][slot_src];
        float* dest = slot_trg == 14 ? chompi_value : values[mode_trg][bank_trg][slot_trg];
        for(size_t i = 0; i < kMaxControls; i++)
        {
            dest[i] = src[i];
        }

        bool valid = slot_src == 14 ? chompi_valid : values_valid[mode_src][bank_src][slot_src];
        if(slot_trg == 14)
            chompi_valid = valid;
        else
            values_valid[mode_trg][bank_trg][slot_trg] = valid;
    
        updated = true;
    }



    float values[kMaxModes][kMaxBanks][kMaxSlots][kMaxControls];
    bool values_valid[kMaxModes][kMaxBanks][kMaxSlots];

    float chompi_value[kMaxControls]; // only store the chompi settings in memory
    bool chompi_valid;

    private:
        bool updated;
        const float* defaults_;
};
} // namespace chompi