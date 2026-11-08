









#pragma once

namespace faloya {





class ProgressStep {
public:
    explicit ProgressStep(const string &label, bool announce = false);
    ~ProgressStep();
    ProgressStep(const ProgressStep &) = delete;
    ProgressStep &operator=(const ProgressStep &) = delete;
};



void ProgressPrint(const string &text);



void ProgressOutput(const char *data, size_t size);



bool ProgressLive();





bool StderrColor(bool force);



void ProgressPlain();

}  
