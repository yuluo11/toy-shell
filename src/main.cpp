#include<iostream>
#include<string>
#include<cerrno>
#include<cstdio>
#include<cstring>
#include<sstream>
#include<vector>
#include<filesystem>
#include<optional>

#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<fcntl.h>


//这里要考虑可重入问题不能用cont，cerr等
namespace{
    volatile sig_atomic_t g_terminal_signal = 0;
    
    void sigsegvHandler(int signal_number) {
        g_terminal_signal = signal_number;

        const char newline = '\n';
        (void)write(STDOUT_FILENO, &newline, 1);
    }

}

namespace fs=std::filesystem;

std::optional<fs::path>pre_dir;

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

        
        
        
        if(line.empty())
            continue;
        
        std::istringstream is(line);
        std::vector<std::string> tokens;
        std::string token;

        //分词
        for(std::string word;is>>word;)
            tokens.push_back(std::move(word));

        //判空
        if(tokens.empty())
            continue;

        //解析重定向
        std::vector<std::string>cmd_args;
        std::optional<std::string>infile,outfile;

        bool is_append=false;
        
        const char* syntax_error = nullptr;
        const auto is_op=[](const std::string& token) {
            return token=="<"||token==">"||token ==">>"||
                    token =="|"|| token=="&";
        };

        for(std::size_t i=0;i<tokens.size();i++){
            const auto & token=tokens[i];

            if(!is_op(token)){
                cmd_args.push_back(token);
                continue;
            }

            if(i+1==tokens.size()|| is_op(token)){
                syntax_error="expected a filename after redirection operator";
                break;
            }

            const bool is_in=token=="<";
            auto& file=is_in?infile:outfile;
            //防止重复重定向
            if(file.has_value()){
                syntax_error=is_in?"duplicate input redirection":"duplicate output redirection";
                break;
            }

            file=tokens[++i];

            //判断是否追加
            if(!is_in)
                is_append=(token==">>");
        }
        if(syntax_error||cmd_args.empty()){
            std::cerr<<(syntax_error?syntax_error:"missing command")<<std::endl;
            lstatus=2;
            continue;
        }

        //处理内置命令
        if(cmd_args[0]=="exit")
            break;
        
        if(cmd_args[0]=="cd"){
            if(cmd_args.size()!=2){
                std::cerr<<"cd: expected one argument\n";
                lstatus=2;
                continue;
            }
            const bool go_back=tokens[1]=="-";

            if(go_back&&!pre_dir){
                std::cerr<<"cd: no previous dir\n";
                lstatus=1;
                continue;
            }

            const fs::path target=go_back?*pre_dir:fs::path(tokens[1]);

            //关闭异常抛出
            std::error_code ec;
            const fs::path cur_dir=fs::current_path(ec);

            if(ec){
                std::cerr<<"cd cannot get current dir\n";
                lstatus=1;
                continue;
            }

            if(chdir(target .c_str())==-1){
                std::perror("cd");
                lstatus=1;
                continue;
            }

            pre_dir=cur_dir;
            lstatus=0;

            if (go_back) {
            std::cout << target.string() << '\n';
            }

            continue;
        }

        //构造argv
        std::vector< char*> argv;

        for(std::string &arg:cmd_args)
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

                //输入重定向
                if(infile.has_value()){
                    int in_fd=open(infile->c_str(),O_RDONLY);

                    if(in_fd==-1){
                        std::perror(infile->c_str());
                        _exit(1);
                    }

                    if(in_fd!=STDIN_FILENO){
                        if(dup2(in_fd,STDIN_FILENO)==-1){
                            std::perror("dup2");
                            close(in_fd);
                            _exit(1);
                        }
                        close(in_fd);
                    }
                }

                //输出重定向
                if(outfile.has_value()){
                    int flags=O_WRONLY|O_CREAT|(is_append?O_APPEND:O_TRUNC);
                    int out_fd=open(outfile->c_str(),flags,0666);

                    if(out_fd==-1){
                        std::perror(outfile->c_str());
                        _exit(1);
                    }

                    if(out_fd!=STDOUT_FILENO){
                        if(dup2(out_fd,STDOUT_FILENO)==-1){
                            std::perror("dup2");
                            close(out_fd);
                            _exit(1);

                        }
                        close(out_fd);
                    }
                }
                
                if(execvp(argv[0], argv.data()) == -1) {
                    int exec_error = errno;

                    std::fprintf(
                        stderr,
                        "toysh: %s: %s\n",
                        cmd_args[0].c_str(),
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