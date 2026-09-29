#pragma once
#include "FileStreamingManager.h"
#include "core_json.h"

#define OPT_VERSION "1"

namespace chompi {
class OptionsManager 
{
    public:
    OptionsManager() {}
    ~OptionsManager() {}

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

    /** Init with defaults,
        open the file and parse, if valid overriding defaults,
        write a new file no matter what
    */
    void Init()
    {
        // fill in defaults
        record_latch = false;
        midi_ch_in = 0;
        midi_ch_out = 0;
        tape_slew_on = true;
        monitor_position = 0;
        pitch_shift_quantization = true;
        delay_split = false;

        /** TODO: make sure the open settings are correct */
        const char fname[32] = "options.json";

        FRESULT res = f_stat(fname, nullptr);

        // create if not exist, don't overwrite
        f_open(&fptr_opt, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

        UINT br;
        f_read(&fptr_opt, &opt_file[0], kOptFileSize, &br);

        if(res == FR_OK)
            Parse();

        WriteFile();
    }

    /** Write a JSON file with whatever settings we have store locally
        These might be defaults, or whatever we parsed from the file most recently
    */
    void WriteFile()
    {
        // header
        std::fill_n(opt_file, kOptFileSize, '\0');
        strcpy(opt_file, "{\n\t\"chompi\": [\n\t\t{\n\t\t\t\"name\": \"Record Latch\",\n\t\t\t\"value\": ");

        // rec latch
        char append[72];
        sprintf(append, "%s", record_latch ? "true" : "false");
        StrAppend(opt_file, append);

        // midi in channel
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Midi In Channel\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", midi_ch_in + 1);
        StrAppend(opt_file, append);

        // midi out channels
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Midi Out Channel\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(midi_ch_out + 1));
        StrAppend(opt_file, append);

        // Tape Slew On
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Tape Slew On\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", tape_slew_on ? "true" : "false");
        StrAppend(opt_file, append);

        // Monitor Position
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Monitor Position\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(monitor_position + 1));
        StrAppend(opt_file, append);

        // Pitch Shift Quantization. true == quant in shift menu (default), false == quant in normal menu
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Pitch Quantize In Shift Menu\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", pitch_shift_quantization ? "true" : "false");
        StrAppend(opt_file, append);

        // Delay Split
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Split Delay\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", delay_split ? "true" : "false");
        StrAppend(opt_file, append);

        // footer
        sprintf(append, "\n\t\t}\n\t]\n}");
        StrAppend(opt_file, append);

        // write the file
        UINT bw = 0;

        f_lseek(&fptr_opt, 0);
        f_write(&fptr_opt, opt_file, strlen(opt_file), &bw);
        f_truncate(&fptr_opt);
        f_sync(&fptr_opt);
    }

    /** loads the JSON file, storing in name/value pairs for all keys */
    void Parse()
    {
        JSONStatus_t json_res;

        size_t len = strlen(opt_file);
        json_res = JSON_Validate(opt_file, len);

        // is the file valid
        if(json_res == JSONSuccess)
        {
            char   query[51];
            char*  value;
            size_t value_len;

            /** TODO: ? Parse version number. Don't think we'll actually need this tbh  */

            for(size_t i = 0; i < kNumOptions; i++)
            {
                sprintf(query, "chompi[%d].name", i);
                json_res = JSON_Search(
                    opt_file, len, query, strlen(query), &value, &value_len);
                char save = value[value_len];
                value[value_len] = '\0';

                int field = -1;

                if(strcmp(value, "Record Latch") == 0 && json_res == JSONSuccess)
                    field = 0;
                if(strcmp(value, "Midi In Channel") == 0 && json_res == JSONSuccess)
                    field = 1;
                if(strcmp(value, "Midi Out Channel") == 0 && json_res == JSONSuccess)
                    field = 2;
                if(strcmp(value, "Tape Slew On") == 0 && json_res == JSONSuccess)
                    field = 3;
                if(strcmp(value, "Monitor Position") == 0 && json_res == JSONSuccess)
                    field = 4;
                if(strcmp(value, "Pitch Quantize In Shift Menu") == 0 && json_res == JSONSuccess)
                    field = 5;
                if(strcmp(value, "Split Delay") == 0 && json_res == JSONSuccess)
                    field = 6;

                value[value_len] = save;

                if(field == 0 || field == 3 || field == 5 || field == 6)
                {
                    sprintf(query, "chompi[%d].value", i);
                    json_res = JSON_Search(
                        opt_file, len, query, strlen(query), &value, &value_len);
                    save = value[value_len];
                    value[value_len] = '\0';

                    if(strcmp(value, "true") == 0 && json_res == JSONSuccess && field == 0)
                        record_latch = true;
                    else if(strcmp(value, "false") == 0 && json_res == JSONSuccess && field == 3)
                        tape_slew_on = false;
                    else if(strcmp(value, "false") == 0 && json_res == JSONSuccess && field == 5)
                        pitch_shift_quantization = false;
                    else if(strcmp(value, "true") == 0 && json_res == JSONSuccess && field == 6)
                        delay_split = true;

                    value[value_len] = save;
                }
                else if(field == 1 || field == 2 || field == 4)
                {
                    sprintf(query, "chompi[%d].value", i);
                    json_res = JSON_Search(
                        opt_file, len, query, strlen(query), &value, &value_len);
                    save = value[value_len];
                    value[value_len] = '\0';

                    const uint8_t val = atoi(value) - 1;
                    // midi channels
                    if(val < 16 && json_res == JSONSuccess && (field == 1 || field == 2))
                    {
                        if(field == 1)
                            midi_ch_in = val;
                        else if(field == 2)
                            midi_ch_out = val;
                    }
                    // monitor position
                    else if(val > 0 && val < 3 && field == 4)
                    {
                        monitor_position = val;
                    }

                    value[value_len] = save;
                }
            }
        }
    }

    bool record_latch;
    uint8_t midi_ch_in;
    uint8_t midi_ch_out;
    bool tape_slew_on;
    uint8_t monitor_position;
    bool delay_split;
    
    /**
    * if true, the shift menu is quantized, and normal is not.
    * if false, normal is quantized, and the shift menu is not.
    */
    bool pitch_shift_quantization;

    private:
        FIL fptr_opt;

        static const size_t kOptFileSize = 4096;
        static const size_t kNumOptions = 7;
        char opt_file[kOptFileSize];
};
} // namespace chompi