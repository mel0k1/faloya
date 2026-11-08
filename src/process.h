





#pragma once

namespace faloya {

struct ProcessResult {
    bool started = false;   
    int code = -1;          
    string output;          
};







ProcessResult RunProcess(const vector<string> &argv, const string &cwd = "", bool echo = false);






string FindOnPath(const string &name, const char *path = nullptr);



void SetEnvVar(const string &name, const string &value);





class FileLock {
    intptr_t handle = -1;
public:
    FileLock(const string &path, const string &waiting);
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
};





bool LoadSharedSymbols(const string &path, vector<pair<string, const void *>> &out, string &err);



string MacSdkPath();



string CommandLineText(const vector<string> &argv);

}  
