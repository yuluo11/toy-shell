#include<iostream>
#include<string>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include<signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//这里要考虑可重入问题不能用cont，cerr等
namespace{
    volatile sig_atomic_t g_terminal_signal = 0;
    
    void sigsegvHandler(int signal_number) {
        g_terminal_signal = signal_number;

        const char newline = '\n';
        (void)write(STDOUT_FILENO, &newline, 1);
    }

}

int lstatus=0;

//这里没有用argc，argv是因为我这是简易版shell没有-c -p这些功能，只需要简单的交互
int main(){ 
    struct sigaction sa={0};
    sa.sa_handler=sigsegvHandler;
    sa.sa_flags=0;
    
    if(sigemptyset(&sa.sa_mask)==-1){
        std::perror("sigemptyst");
        return 1;
    }

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::perror("sigaction(SIGINT)");
        return 1;
    }

    if (sigaction(SIGQUIT, &sa, nullptr) == -1) {
        std::perror("sigaction(SIGQUIT)");
        return 1;
    }

    while(1){
        std::cout<<"toysh>"<<std::flush;
        std::string line;

        if(!getline(std::cin,line)){

            //这里ctrl+c可能会终止getline，此时要清除iostream状态码
            if (g_terminal_signal != 0) {
                std::cin.clear();
                g_terminal_signal = 0;
                continue;
            }
            
            //正常的EOF不算错误
            if (std::cin.eof()) {
                break;
            }

            std::cerr << "toysh: failed to read input\n";
            return 1;
        }
        

        if (g_terminal_signal != 0) {
            g_terminal_signal = 0;
            continue;
        }

        
        if(line=="exit")
            break;
        
        if(line.empty())
            continue;
        
        std::istringstream is(line);
        std::vector<std::string> tokens;
        std::string token;

        while(is>>token)
            tokens.push_back(token);
        
        if(tokens.empty())
            continue;

        std::vector< char*> argv;

        for(std::string &arg:tokens)
            argv.push_back(arg.data());

        argv.push_back(nullptr);

        pid_t pid=fork();
        
        switch(pid){
            case -1:{
            // fork 失败
            std::perror("toysh: fork");
            lstatus=1;
            continue;
        }

            case 0:{
                // 子进程分支
                struct sigaction da={0};
                da.sa_handler=SIG_DFL;
                da.sa_flags=0;

                if(sigemptyset(&da.sa_mask)==-1) {
                    std::perror("sigemptyset");
                    _exit(126);
                }

                if(sigaction(SIGINT,&da,nullptr)==-1) {
                    std::perror("sigaction(SIGINT)");
                    _exit(126);
                }

                if(sigaction(SIGQUIT,&da,nullptr)==-1) {
                    std::perror("sigaction(SIGQUIT)");
                    _exit(126);
                }

                if(execvp(argv[0], argv.data()) == -1) {
                    int exec_error = errno;

                    std::fprintf(
                        stderr,
                        "toysh: %s: %s\n",
                        tokens[0].c_str(),
                        std::strerror(exec_error)
                    );

                    if(exec_error == ENOENT) {
                        _exit(127);
                    }

                    _exit(126);
                }

                // execvp 成功时不会返回。
                _exit(127);
            }

            default:{
                // 父进程分支。
                // fork() 在父进程中返回子进程的 PID，必定大于 0。

                int cstatus=0;
                bool succ=false;

                while(true){
                    pid_t result=waitpid(pid,&cstatus,0);

                    if(result==pid) {
                        succ=true;
                        break;
                    }

                    if(result==-1&&errno ==EINTR) {
                        continue;
                    }

                    if(result==-1) {
                        std::perror("waitpid");
                        lstatus=1;
                        break;
                    }
                }

                if (succ){
                    if(WIFEXITED(cstatus)){
                        lstatus=WEXITSTATUS(cstatus);
                    } else if(WIFSIGNALED(cstatus)){
                        lstatus=128+WTERMSIG(cstatus);
                    } else{
                        lstatus=1;
                    }
                }

                break;
            }
        }
    }
    return 0;
}