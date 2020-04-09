#include <unistd.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "Commands.h"
#include <limits>

using namespace std;

// definition of CURR_FORK_CHILD_RUNNING
pid_t CURR_FORK_CHILD_RUNNING = 0;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cerr << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cerr << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

#define DEBUG_PRINT cerr << "DEBUG: "

#define EXEC(path, arg) \
  execvp((path), (arg));

string _ltrim(const std::string& s)
{
  size_t start = s.find_first_not_of(WHITESPACE);
  return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string& s)
{
  size_t end = s.find_last_not_of(WHITESPACE);
  return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string& s)
{
  return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char* cmd_line, char** args) {
  FUNC_ENTRY()
  int i = 0;
  std::istringstream iss(_trim(string(cmd_line)).c_str());
  for(std::string s; iss >> s; ) {
    args[i] = (char*)malloc(s.length()+1);
    memset(args[i], 0, s.length()+1);
    strcpy(args[i], s.c_str());
    args[++i] = nullptr;
  }
  return i;

  FUNC_EXIT()
}

bool _isBackgroundComamnd(const char* cmd_line) {
  const string str(cmd_line);
  return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char* cmd_line) {
  const string str(cmd_line);
  // find last character other than spaces
  unsigned int idx = str.find_last_not_of(WHITESPACE);
  // if all characters are spaces then return
  if (idx == string::npos) {
    return;
  }
  // if the command line does not end with & then return
  if (cmd_line[idx] != '&') {
    return;
  }
  // replace the & (background sign) with space and then remove all tailing spaces.
  cmd_line[idx] = ' ';
  // truncate the command line string up to the last non-space character
  cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

bool checkAndRemoveAmpersand(string& str) {
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);

    // if all characters are spaces then return
    if (idx == string::npos) {
        return false;
    }

    // if the command line does not end with & then return
    if (str[idx] != '&') {
        return false;
    }

    // erase
    str.erase(idx);
}

//---------------------------JOBS LISTS------------------------------
JobEntry::JobEntry(pid_t pid, const string& cmd_str, bool is_stopped) :  pid(pid),
                                                                         cmd_str(cmd_str),
                                                                         is_stopped(is_stopped) {
    start_time = time(nullptr);
    if (start_time == (time_t)(-1)) perror("smash error: time failed");

}
void JobsList::addJob(pid_t pid, const string& cmd_str, bool is_stopped) {
    // remove zombies from jobs list
    removeFinishedJobs();

    // create new job entry
    JobEntry new_job(pid, cmd_str, is_stopped);
    JobID new_id = 1;
    if (!jobs.empty()) new_id = jobs.rbegin()->first + 1;

    // insert to map
    jobs[new_id] = new_job;
}
void JobsList::printJobsList() {
    // remove zombies from jobs list
    removeFinishedJobs();

    // iterate the map and print each job by the format
    for (const auto& job : jobs) {
        auto curr_time = time(nullptr);
        if (curr_time == (time_t)(-1)) perror("smash error: time failed");
        auto diff_time = difftime(curr_time, job.second.start_time);
        if (diff_time == (time_t)(-1)) perror("smash error: difftime failed");

        cout << "[" << job.first << "]";
        cout << " " << job.second.cmd_str;
        cout << " : " << job.second.pid;
        cout << " " << diff_time << " secs";
        if (job.second.is_stopped) cout << " (stopped)";
        cout << endl;
    }
}
void JobsList::killAllJobs() {
    cout << "smash: sending SIGKILL signal to " << jobs.size() << " jobs:" << endl;

    // interate on map, print message and send SIGKILL than wait them
    for (const auto& job : jobs) {
        cout << job.second.pid << " " << job.second.cmd_str << endl;
        if (kill(job.second.pid, SIGKILL) < 0) perror("smash error: kill failed");
        if (waitpid(job.second.pid, nullptr, 0) < 0) perror("smash error: waitpid failed");
    }
}
void JobsList::removeFinishedJobs() {
    vector<JobID> to_remove(100,0);
    int to_remove_iter= 0;

    // iterate on map, and waitpid with each pid WNOHANG flag
    for (const auto& job : jobs) {
        pid_t waited = waitpid(job.second.pid, nullptr, WNOHANG);
        if (waited < 0) perror("smash error: waitpid failed");
        if (waited > 0) to_remove[to_remove_iter++] = job.first;
    }

    // remove from map all waited jobs
    for (int i = 0; i < to_remove_iter; i++) {
        jobs.erase(to_remove[i]);
    }
}
JobEntry* JobsList::getJobById(JobID jobId) {
    // remove zombies from jobs list
    removeFinishedJobs();

    if (jobs.count(jobId) == 0) return nullptr;
    // return from map
    return &jobs[jobId];
}
void JobsList::removeJobById(JobID jobId) {
    // remove zombies from jobs list
    removeFinishedJobs();

    if (jobs.count(jobId) > 0) {
        jobs.erase(jobId);
    }
}
JobEntry* JobsList::getLastJob(JobID* lastJobId) {
    // remove zombies from jobs list
    removeFinishedJobs();

    if (jobs.empty()) return nullptr;

    // return last in map
    auto last_job = jobs.rbegin();
    if (lastJobId) *lastJobId = last_job->first;
    return &(last_job->second);
}
JobEntry* JobsList::getLastStoppedJob(JobID* jobId) {
    // remove zombies from jobs list
    removeFinishedJobs();

    // iterate and find last stopped job return it
    for (auto iter = jobs.rbegin(); iter != jobs.rend(); iter++) {
        if (iter->second.is_stopped) {
            if (jobId) *jobId = iter->first;
            return &(iter->second);
        }
    }

    return nullptr;
}
//-------------------------SPECIAL COMMANDS-------------------------
PipeCommand::PipeCommand(const char* cmd_line, SmallShell* shell) : Command(cmd_line) {
    // save cmd and shell
}
void PipeCommand::execute() {
    // create new pipe
    // fork two new childs
        // first child:
        // call setpgrp() syscall - make sure that the child get different GROUP ID
        // close read in pipe
        // put write side of pipe in stdout or stderr (if with |&)
        // shell.execute command with first half of the cmd

        // second child:
        // call setpgrp() syscall - make sure that the child get different GROUP ID
        // close write in pipe
        // put read side in stdin
        // shell.execute command with second half of the cmd

        //father:
        // close both sides of pipe
        // if & add both jobs to jobs list
       // else CURR_FORK =..., waitpid for both of them
        // CURR_ FORK.. =0
}

RedirectionCommand::RedirectionCommand(const char* cmd_line, SmallShell* shell) :   Command(cmd_line),
                                                                                    shell(shell),
                                                                                    to_append(false) {
    // find split place
    int split_place = 0;
    while (cmd_line[split_place] && cmd_line[split_place] != '>') split_place++;
    if (cmd_line[split_place+1] == '>') to_append = true;

    // save command part
    cmd_part = string(cmd_line, split_place);

    // and file address part
    if (to_append) split_place++;
    pathname = cmd_line[split_place+1];
}
void RedirectionCommand::execute() {
    // open file, if to_append == true open in append mode
    int flags = O_CREAT;
    if (to_append) flags |= O_APPEND;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int file_fd = open(pathname.c_str(), flags, mode);
    if (file_fd < 0) perror("smash error: open failed");

    // move stdout to other file descriptor
    int stdout_fd = dup(1);
    if (stdout_fd < 0) perror("smash error: dup failed");

    // put file descriptor in 1st place
    if (dup2(file_fd, 1) < 0) perror("smash error: dup2 failed");

    // shell.execute the command
    shell->executeCommand(cmd_part.c_str());

    // restore stdout and close all new file descriptors
    if (dup2(stdout_fd, 1) < 0) perror("smash error: dup2 failed");;
    if (close(stdout_fd) < 0) perror("smash error: close failed");
    if (close(file_fd) < 0) perror("smash error: close failed");
}

//---------------------------EXTERNAL CLASSES------------------------------
ExternalCommand::ExternalCommand(const char* cmd_line, JobsList* jobs) :    Command(cmd_line),
                                                                            jobs(jobs),
                                                                            to_background(false),
                                                                            cmd_to_son(cmd_line) {
    if (checkAndRemoveAmpersand(cmd_to_son)) to_background = true;
}
void ExternalCommand::execute() {
    pid_t pid = fork();

    if (pid == 0) { //child:
        setpgrp(); // no possible errors

        // exec to bash with cmd_line
        if (execl("/bin/bash", "/bin/bash", cmd_to_son.c_str(), (char*) nullptr) < 0) {
            perror("smash error: execl failed");
        }
    }
    else if (pid > 0) { //parent
        // if with "&" add to JOBS LIST and return
        if (to_background) {
            jobs->addJob(pid, cmd_line);
        } else {
            // wait for job
            // add to jobs list if stopped
            CURR_FORK_CHILD_RUNNING = pid;
            int status;
            if (waitpid(pid, &status, WUNTRACED) < 0) {
                perror("smash error: waitpid failed");
            } else {
                if (WIFSTOPPED(status)) jobs->addJob(pid, cmd_line, true);
            }
            CURR_FORK_CHILD_RUNNING = 0;
        }
    }
    else { // fork failed
        perror("smash error: fork failed");
    }
}

//---------------------------BUILT IN CLASSES------------------------------
ChangePromptCommand::ChangePromptCommand(const char* cmd_line, SmallShell* shell) : BuiltInCommand(cmd_line) {
    // no argument = change to default prompt
    // get first argument
    // save prompt string
    // save shell pointer
}
void ChangePromptCommand::execute() {
    // shell change prompt with string
}

void ShowPidCommand::execute() {
    // "smash pid is:" ...
    // syscall getpid()
}

void GetCurrDirCommand::execute() {
    //
    // syscall getcwd()
}

ChangeDirCommand::ChangeDirCommand(const char* cmd_line, string* last_dir) : BuiltInCommand(cmd_line) {
    // save new_path and last_dir
    // more than one argument == print error "too many argument"
}
void ChangeDirCommand::execute() {
    // if new_path == "" return
    // get curr dir with syspath
    // if new_path == "-" syscall with last_dir
        // if no last_dir print error "..."
    // else, syscall with new_path
    // last_dir = curr dir

    // if syscall fails use perror to print error
}

JobsCommand::JobsCommand(const char* cmd_line, JobsList* jobs) : BuiltInCommand(cmd_line) {
    // save jobs list
}
void JobsCommand::execute() {
    // jobs.print...
}

KillCommand::KillCommand(const char* cmd_line, JobsList* jobs) :    BuiltInCommand(cmd_line),
                                                                    signum(0),
                                                                    job_id(0),
                                                                    jobs(jobs) {
    // parse: type of signal and jobID, if syntax not valid print error
    if (!parseAndCheck(cmd_line, &signum, &job_id)) {
        printArgumentsError();
        return;
    }

    // if job id not exist print error message
    auto job_entry = jobs->getJobById(job_id);
    if (!job_entry) {
        printJobError();
        job_id = 0;
        return;
    }
}
void KillCommand::execute() {
    if (job_id == 0 || signum == 0) return;
    auto job_entry = jobs->getJobById(job_id);

    // send signal, print message
    if (kill(job_entry->pid, signum) < 0) perror("smash error: kill failed");
    printSignalSent(signum, job_entry->pid);

    // if signal was SIGSTOP or SIGTSTP update job state to stopped
    if (signum == SIGSTOP || signum == SIGTSTP) job_entry->is_stopped = true;

    // if signal was SIGCONT update job state to not stopped
    if (signum == SIGCONT) job_entry->is_stopped = false;
}
bool KillCommand::parseAndCheck(const char* cmd_line, int* sig, JobID* j_id) {
    string first_arg, second_arg;

    // parse
    char* args[21];
    int num_of_args = _parseCommandLine(cmd_line, args);
    if (num_of_args == 3) {
        first_arg = args[1];
        second_arg = args[2];
    }
    for (int i = 0; i < num_of_args; i++) free(args[i]);
    if (num_of_args != 3) return false;

    // check first argument
    if ((int)first_arg.size() < 2) return false;
    if ((int)first_arg.size() > 3) return false;
    if (first_arg[0] != '-') return false;
    if (!isdigit(first_arg[1])) return false;
    if ((int)first_arg.size() == 3 && !isdigit(first_arg[2])) return false;
    *sig = stoi(first_arg.substr(1));
    if (*sig < 1 || *sig > 31) return false;

    // check second argument
    if ((int)second_arg.size() > 10) return false;
    for (auto letter : second_arg) if (!isdigit(letter)) return false;
    long job = stol(second_arg);
    if (job > numeric_limits<int>::max() || job < 1) return false;

    // all OK
    *j_id = (int)job;
    return true;
}
void KillCommand::printArgumentsError() {
    cout << "smash error: kill: invalid arguments" << endl;
}
void KillCommand::printJobError() {
    cout << "smash error: kill: job-id " << job_id << " does not exist" << endl;
}
void KillCommand::printSignalSent(int sig, pid_t p) {
    cout << "signal number " << sig << " was sent to pid " << p << endl;
}

ForegroundCommand::ForegroundCommand(const char* cmd_line, JobsList* jobs) :    BuiltInCommand(cmd_line),
                                                                                job_id(0),
                                                                                jobs(jobs) {
    // if num of argument not valid or syntax problem print error
    if (!parseAndCheckFgBgCommands(cmd_line, &job_id)) {
        printArgumentsError();
        job_id = -1;
        return;
    }

    auto job_entry = jobs->getJobById(job_id);
    if (!job_entry) {
        printJobError(job_id);
        job_id = -1;
        return;
    }
}
void ForegroundCommand::execute() {
    if (job_id < 0) return; // error in arguments or job not exist

    JobEntry* job;
    // if job_id == 0 than get the last job
    // if jobs list empty print error
    if (job_id == 0) {
        job = jobs->getLastJob(&job_id);
        if (!job) {
            printNoJobsError();
            return;
        }
    } else {
        job = jobs->getJobById(job_id);
    }
    int pid = job->pid;
    string cmd_str = job->cmd_str;

    // remove from jobs list
    jobs->removeJobById(job_id);

    // print job's command line
    cout << cmd_str << " : " << pid << endl;

    // send SIGCONT to job's pid
    if (kill(pid, SIGCONT) < 0) perror("smash error: kill failed");

    // wait for job
    // add to jobs list if stopped
    CURR_FORK_CHILD_RUNNING = pid;
    int status;
    if (waitpid(pid, &status, WUNTRACED) < 0) {
        perror("smash error: waitpid failed");
    } else {
        if (WIFSTOPPED(status)) jobs->addJob(pid, cmd_line);
    }
    CURR_FORK_CHILD_RUNNING = 0;
}
void ForegroundCommand::printArgumentsError() {
    cout << "smash error: fg: invalid arguments" << endl;
}
void ForegroundCommand::printJobError(JobID job_id) {
    cout << "smash error: fg: job-id " << job_id << " does not exist" << endl;
}
void ForegroundCommand::printNoJobsError() {
    cout << "smash error: fg: jobs list is empty" << endl;
}

BackgroundCommand::BackgroundCommand(const char* cmd_line, JobsList* jobs) : BuiltInCommand(cmd_line),
                                                                             job_id(0),
                                                                             jobs(jobs) {
    // if num of argument not valid or syntax problem print error
    if (!parseAndCheckFgBgCommands(cmd_line, &job_id)) {
        printArgumentsError();
        job_id = -1;
        return;
    }

    auto job_entry = jobs->getJobById(job_id);
    if (!job_entry) {
        printJobError(job_id);
        job_id = -1;
        return;
    }
    if (!job_entry->is_stopped) {
        printNotStoppedError(job_id);
        job_id = -1;
        return;
    }
}
void BackgroundCommand::execute() {
    if (job_id < 0) return; // error in arguments or job not exist

    JobEntry* job = nullptr;
    // if job_id == 0 than get the last job
    // if jobs list empty print error
    if (job_id == 0) {
        job = jobs->getLastStoppedJob(&job_id);
        if (!job) {
            printNoJobsError();
            return;
        }
    } else {
        job = jobs->getJobById(job_id);
    }

    // print job's command line
    cout << job->cmd_str << " : " << job->pid << endl;

    // send SIGCONT to job's pid
    // update is_stopped
    job->is_stopped = false;
    if (kill(job->pid, SIGCONT) < 0) perror("smash error: kill failed");
}
void BackgroundCommand::printArgumentsError() {
    cout << "smash error: b g: invalid arguments" << endl;
}
void BackgroundCommand::printJobError(JobID job_id) {
    cout << "smash error: bg: job-id " << job_id << " does not exist" << endl;
}
void BackgroundCommand::printNoJobsError() {
    cout << "smash error: bg: there is no stopped jobs to resume" << endl;
}
void BackgroundCommand::printNotStoppedError(JobID job_id) {
    cout << "smash error: bg: job-id " << job_id << " is already running in the background" << endl;
}

bool parseAndCheckFgBgCommands(const char* cmd_line, JobID* job_id) {
    string arg;

    // parse
    char* args[21];
    int num_of_args = _parseCommandLine(cmd_line, args);
    if (num_of_args == 2) arg = args[1];
    for (int i = 0; i < num_of_args; i++) free(args[i]);

    if (num_of_args > 2) return false;
    if (num_of_args == 1) return true;

    for (auto letter : arg) if (!isdigit(letter)) return false;
    long job = stol(arg);
    if (job > numeric_limits<int>::max() || job < 1) return false;

    // all OK
    *job_id = (int)job;
    return true;
}

QuitCommand::QuitCommand(const char* cmd_line, JobsList* jobs) :    BuiltInCommand(cmd_line),
                                                                    kill_all(false),
                                                                    jobs(jobs) {
    // parse
    char* args[21];
    int num_of_args = _parseCommandLine(cmd_line, args);
    if (num_of_args > 1) {
        string arg = args[1];
        if (arg == "kill") kill_all = true;
    }
    for (int i = 0; i < num_of_args; i++) free(args[i]);
}
void QuitCommand::execute() {
    if (kill_all) jobs->killAllJobs();
    exit(0);
}

CopyCommand::CopyCommand(const char* cmd_line) : BuiltInCommand(cmd_line) {
    // save cmd line;
}
void CopyCommand::execute() {
    // open the new file place and the prev file
    // fork
        // child
        // setpgrp
        // loop:
        // read from prev and write to new file in chuncks

        // father
        // if with & add to job list
        // else CURR_FORK =..., waitpid
        // CURR_ FORK.. =0
}
//---------------------------END OF BUILT IN--------------------------------

//---------------------------SMALL SHELL--------------------------------------
SmallShell::SmallShell() {
    // prompt = Smash
    // CURR_FORK.. = 0
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command* SmallShell::CreateCommand(const char* cmd_line) {
	// For example:
    // > >> | |&
    // if one of those, create special class
/*
  string cmd_s = string(cmd_line);
  if (cmd_s.find("pwd") == 0) {
    return new GetCurrDirCommand(cmd_line);
  }
  else if ...
  .....
  else {
    return new ExternalCommand(cmd_line, jobs);
  }
  */
  return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
  // TODO: Add your implementation here
  // for example:
  // Command* cmd = CreateCommand(cmd_line);
  // maybe nullptr
  // cmd->execute();
  // Please note that you must fork smash process for some commands (e.g., external commands....)
  // delete class
}

void SmallShell::changePrompt(string prompt) {

}

string& SmallShell::getPrompt() {

}