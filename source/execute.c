#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <dirent.h>

#include "global.h"

int goon = 0, ingnore = 0;       //用于设置signal信号量
char *envPath[10], cmdBuff[40];  //外部命令的存放路径及读取外部命令的缓冲空间
History history;                 //历史命令
Job *head = NULL;                //作业头指针
pid_t fgPid;                     //当前前台作业的进程号

/*******************************************************
                  工具以及辅助方法
********************************************************/
/*判断命令是否存在*/
int exists(char *cmdFile)
{
    int i = 0;
    if((cmdFile[0] == '/' || cmdFile[0] == '.') && access(cmdFile, F_OK) == 0)		//命令在当前目录，不是特别明白？？？？
    { 
        strcpy(cmdBuff, cmdFile);
        return 1;
    }
    else										//查找ysh.conf文件中指定的目录，确定命令是否存在
    {  
        while(envPath[i] != NULL)							//查找路径已在初始化时设置在envPath[i]中
	{ 
            strcpy(cmdBuff, envPath[i]);
            strcat(cmdBuff, cmdFile);
            if(access(cmdBuff, F_OK) == 0)						 //在cmdBuff所提供的目录下，命令文件被找到
                return 1; 
            i++;
        }
    } 
    return 0; 
}

/*将字符串转换为整型的Pid*/
int str2Pid(char *str, int start, int end)
{
    int i, j;
    char chs[20];    
    for(i = start, j= 0; i < end; i++, j++)
    {
        if(str[i] < '0' || str[i] > '9')
            return -1;
	else
            chs[j] = str[i];
    }
    chs[j] = '\0';
    return atoi(chs);
}

/*调整部分外部命令的格式*/
void justArgs(char *str){
    int i, j, len;
    len = strlen(str);
    
    for(i = 0, j = -1; i < len; i++){
        if(str[i] == '/'){
            j = i;
        }
    }

    if(j != -1){ //找到符号'/'
        for(i = 0, j++; j < len; i++, j++){
            str[i] = str[j];
        }
        str[i] = '\0';
    }
}

/*设置goon*/
void setGoon()
{
    goon = 1;
}

/*释放环境变量空间*/
void release()
{
    int i;
    for(i = 0; strlen(envPath[i]) > 0; i++)
    {
        free(envPath[i]);
    }
}

/*******************************************************
                  信号以及jobs相关
********************************************************/
/*添加新的作业*/
Job* addJob(pid_t pid){
    Job *now = NULL, *last = NULL, *job = (Job*)malloc(sizeof(Job));
    
	//初始化新的job
    job->pid = pid;
    strcpy(job->cmd, inputBuff);
    strcpy(job->state, RUNNING);
    job->next = NULL;
    
    if(head == NULL){ //若是第一个job，则设置为头指针
        head = job;
    }else{ //否则，根据pid将新的job插入到链表的合适位置
		now = head;
		while(now != NULL && now->pid < pid){
			last = now;
			now = now->next;
		}
        last->next = job;
        job->next = now;
    }
    
    return job;
}

/*移除一个作业*/
void rmJob(int sig, siginfo_t *sip, void* noused){
//	printf("rmJob\n");
    pid_t pid;
    Job *now = NULL, *last = NULL;
    
  //  printf("%d\n", ingnore);
    if(ingnore == 1){
        ingnore = 0;
        return;
    }
    
    pid = sip->si_pid;

    now = head;
	while(now != NULL && now->pid == pid){
		last = now;
		now = now->next;
	}
    
    if(now == NULL){ //作业不存在，则不进行处理直接返回
        return;
    }
    
	//开始移除该作业
    if(now == head){
        head = now->next;
    }else{
        last->next = now->next;
    }
    
    free(now);
}

/*组合键命令ctrl+z*/
void ctrl_Z(){
    Job *now = NULL;
    
    if(fgPid == 0) //前台没有作业则直接返回
        return;
    
    //SIGCHLD信号产生自ctrl+z
    ingnore = 1;
    
	now = head;
	while(now != NULL && now->pid != fgPid)
		now = now->next;
    
    if(now == NULL) //未找到前台作业，则根据fgPid添加前台作业
        now = addJob(fgPid); 
//修改前台作业的状态及相应的命令格式，并打印提示信息
    strcpy(now->state, STOPPED); 
    now->cmd[strlen(now->cmd)] = '&';
    now->cmd[strlen(now->cmd) + 1] = '\0';
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd); 
   //发送SIGTSTP信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGSTOP);
    fgPid = 0;
}

/*组合键命令ctrl+c*/
void ctrl_C(){
    Job *now = NULL;
    if(fgPid == 0) //前台没有作业则直接返回
        return;
    now = head;
    while(now != NULL && now->pid != fgPid)
	now = now->next;
//发送SIGINT信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGINT);
	printf("dashabi\n");
    fgPid = 0;
}

/*fg命令*/
void fg_exec(int pid)
{    
    Job *now = NULL; 
    int i;
//SIGCHLD信号产生自此函数
    ingnore = 1;  
//根据pid查找作业
    now = head;
	while(now != NULL && now->pid != pid)
		now = now->next;
    if(now == NULL){ //未找到作业
        printf("7%d pid not exists!\n", pid);
        return;
    }

    //记录前台作业的pid，修改对应作业状态
    fgPid = now->pid;
    strcpy(now->state, RUNNING);
    
    signal(SIGTSTP, ctrl_Z); 				//设置signal信号，为下一次按下组合键Ctrl+Z做准备
    signal(SIGINT, ctrl_C);				//设置signal信号，为下一次按下组合键Ctrl+C做准备
    i = strlen(now->cmd) - 1;
    while(i >= 0 && now->cmd[i] != '&')
		i--;
    now->cmd[i] = '\0';
    printf("%s\n", now->cmd);
    kill(now->pid, SIGCONT); 				//向对象作业发送SIGCONT信号，使其运行
    sleep(100);
    waitpid(fgPid, NULL, 0); 				//父进程等待前台进程的运行
}

/*bg命令*/
void bg_exec(int pid){
    Job *now = NULL;
    //SIGCHLD信号产生自此函数
    ingnore = 1;				//信号函数的信号量
    now = head;
    while(now != NULL && now->pid != pid)    //根据pid查找作业
	now = now->next;
    if(now == NULL)				//未找到作业
    { 
        printf("7%d pid not exists!\n", pid);
        return;
    }
    strcpy(now->state, RUNNING); 		//修改对象作业的状态
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);
    
    kill(now->pid, SIGCONT); 			//向对象作业发送SIGCONT信号，使其运行
    fgPid=0;					//一旦被放到后台，前台就没有了
}

/*******************************************************
                    命令历史记录
********************************************************/
void addHistory(char *cmd)
{
    if(history.end == -1)				//第一次使用history命令
    { 
        history.end = 0;
        strcpy(history.cmds[history.end], cmd);
        return;
    }
    history.end = (history.end + 1)%HISTORY_LEN; 	//end前移一位
    strcpy(history.cmds[history.end], cmd); 		//将命令拷贝到end指向的数组中
    
    if(history.end == history.start) 			//end和start指向同一位置
        history.start = (history.start + 1)%HISTORY_LEN; //start前移一位
}

/*******************************************************
                     初始化环境
********************************************************/
/*通过路径文件获取环境路径*/
void getEnvPath(int len, char *buf){
    int i, j, last = 0, pathIndex = 0, temp;
    char path[40];
    
    for(i = 0, j = 0; i < len; i++){
        if(buf[i] == ':'){ //将以冒号(:)分隔的查找路径分别设置到envPath[]中
            if(path[j-1] != '/'){
                path[j++] = '/';
            }
            path[j] = '\0';
            j = 0;
            
            temp = strlen(path);
            envPath[pathIndex] = (char*)malloc(sizeof(char) * (temp + 1));
            strcpy(envPath[pathIndex], path);
            
            pathIndex++;
        }else{
            path[j++] = buf[i];
        }
    }
    
    envPath[pathIndex] = NULL;
}

/*初始化操作*/
void init(){
    int fd, n, len;
    char c, buf[80];

	//打开查找路径文件ysh.conf
    if((fd = open("ysh.conf", O_RDONLY, 660)) == -1){
        perror("init environment failed\n");
        exit(1);
    }
    
	//初始化history链表
    history.end = -1;
    history.start = 0;
    
    len = 0;
	//将路径文件内容依次读入到buf[]中
    while(read(fd, &c, 1) != 0){ 
        buf[len++] = c;
    }
    buf[len] = '\0';

    //将环境路径存入envPath[]
    getEnvPath(len, buf); 
    
    //注册信号
    struct sigaction action;
    action.sa_sigaction = rmJob;
    sigfillset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &action, NULL);
    signal(SIGTSTP, ctrl_Z);
    signal(SIGINT, ctrl_C);
}

/*******************************************************
                      命令解析函数
********************************************************/
SimpleCmd* handleSimpleCmdStr(int begin, int end)
{
    int i, j, k;
    int fileFinished; 							//记录命令是否解析完毕
    char c, buff[10][40], inputFile[30], outputFile[30], *temp = NULL;
    SimpleCmd *cmd = (SimpleCmd*)malloc(sizeof(SimpleCmd));

//默认为非后台命令，输入输出重定向为null
    cmd->isBack = 0;
    cmd->input = cmd->output = NULL;
    for(i = begin; i<10; i++)					//初始化相应变量
        buff[i][0] = '\0';
    inputFile[0] = '\0';
    outputFile[0] = '\0';   
    i = begin;
    while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t'))		//跳过空格等无用信息
        i++;
    k = 0;
    j = 0;
    fileFinished = 0;
    temp = buff[k]; 							//以下通过temp指针的移动实现对buff[i]的顺次赋值过程
    while(i < end)
    {		
        switch(inputBuff[i])						//根据命令字符的不同情况进行不同的处理*/
       { 							
            case ' ':
            case '\t': 							//命令名及参数的结束标志
                temp[j] = '\0';
                j = 0;
                if(!fileFinished)
		{
                    k++;
                    temp = buff[k];					//开始下一个字符串搜集
                }
                break;
            case '<': 							//输入重定向标志
                if(j != 0)						//此判断为防止命令直接挨着<符号导致判断为同一个参数，如果ls<sth
		{		    
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished)					//说明没有遇到重定向文件（准确的说是输出重定向）
		    {
                        k++;
                        temp = buff[k];
                    }
                }
                temp = inputFile;					//下面开始查找当前的inputfile
                fileFinished = 1;					//说明遇到了重定向文件
                i++;
                break;
            case '>': 							//输出重定向标志
                if(j != 0)
		{
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished)
		    {
                        k++;
                        temp = buff[k];
                    }
                }
                temp = outputFile;
                fileFinished = 1;
                i++;
                break;
            case '&': 							//后台运行标志
                if(j != 0)
		{
		    temp[j]='&';
                    temp[++j] = '\0';
                    j = 0;
                    if(!fileFinished)
		    {
                        k++;
                        temp = buff[k];
                    }
                }
                cmd->isBack = 1;
                fileFinished = 1;					//这里要这个干啥呢？？？？
                i++;
                break;
            default: 					//默认则读入到temp指定的空间
                temp[j++] = inputBuff[i++];
                continue;
	}	
        while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t'))		//跳过空格等无用信息
            i++;
    }
    printf("%s\n",buff[0]);
    if(inputBuff[end-1] != ' ' && inputBuff[end-1] != '\t' && inputBuff[end-1] != '&')
    {
        temp[j] = '\0';
        if(!fileFinished)
            k++;
    }
    
//依次为命令名及其各个参数赋值
    cmd->args = (char**)malloc(sizeof(char*) * (k + 1));
    cmd->args[k] = NULL;
    for(i = 0; i<k; i++)
    {
        j = strlen(buff[i]);
        cmd->args[i] = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->args[i], buff[i]);
    }
//如果有输入重定向文件，则为命令的输入重定向变量赋值
    if(strlen(inputFile) != 0)
    {
        j = strlen(inputFile);
        cmd->input = (char*)malloc(sizeof(char) * (j + 1));
        strcpy(cmd->input, inputFile);
    }

    //如果有输出重定向文件，则为命令的输出重定向变量赋值
    if(strlen(outputFile) != 0)
    {
        j = strlen(outputFile);
        cmd->output = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->output, outputFile);
    }
    return cmd;
}

/*******************************************************
                      命令执行
********************************************************/
/******************第一类指令：执行管道命令****************/
void ExecPipe(SimpleCmd *pipeCmd[128],int pipeNum){
    int i=0;
    int pipeIn, pipeOut;
    int pipe_fd[128][2];					//定义多个管道
    pid_t pid_child[128];					//定义各进程号

    if(pipe(pipe_fd[i])<0)
    {
	printf("创建管道失败\n");
	return;
    }
    if((pid_child[0]=fork())<0)
    {
	perror("Fork failed");
	exit(errno);
    }
    if(!pid_child[0] ) 						//创建子进程pid_child[0]
    { 
	close(pipe_fd[i][0]);					//关闭管道的输入描述符，不能往管道里面写东西了。
	dup2(pipe_fd[i][1], 1);					//将管道的输出描述符复制到进程的标准输出，也就是是说标准输出现在指向管道的输出描述符
	close(pipe_fd[i][1]);					//关闭进程的标准输入文件
	if(pipeCmd[i]->input != NULL)				//存在输入重定向
	{ 
                if((pipeIn = open(pipeCmd[i]->input, O_RDONLY, S_IRUSR|S_IWUSR)) == -1)
		{
                    printf("不能打开文件 %s！\n", pipeCmd[i]->input);
                    return;
                }
                if(dup2(pipeIn, 0) == -1)			//将当前进程的标准输入指向pipeIn
		{
                    printf("输入重定向错误！\n");
                    return;
                }
        }	
	if(exists(pipeCmd[i]->args[0]))				//命令存在执行命令
	{ 
            if(execve(cmdBuff, pipeCmd[i]->args,NULL) < 0)	//执行当前对应的命令
	    { 
                printf("execv failed!\n");
                return;
            }
   	}
    }
    close(pipe_fd[i][1]);					//关闭管道的输出文件描述符
    waitpid(pid_child[0], NULL, 0);				//父进程等待前台进程的运行
    i++;
    while(i<pipeNum-1)
    {
	if(pipe(pipe_fd[i])<0)
	{
		printf("创建管道失败\n");
		return;
	}
	if((pid_child[i]=fork())<0)
	{
		perror("Fork failed");
		exit(errno);
	}
	if(!pid_child[i])					//创建子进程
	{
		close(pipe_fd[i-1][1]);				//关闭当前管道的输出描述符		
		//close(pipe_fd[i-1][0]);				//关闭上一个管道的写入描述符
		dup2(pipe_fd[i-1][0],0);			//将上一个管道的写入描述符复制到进程的标准输入
		close(pipe_fd[i-1][0]);				//关闭上一管道的输出
		dup2(pipe_fd[i][1],1);				//将当前管道的写描述符复制到标准输出
		close(pipe_fd[i][1]);				//关闭当前管道的输出文件描述符
		if(exists(pipeCmd[i]->args[0]))			//存在执行命令
		{ 
            		if(execve(cmdBuff, pipeCmd[i]->args,NULL) < 0)
			{ 
                		printf("execv failed!\n");
                		return;
			}
   		}
	}
	close(pipe_fd[i][1]);					//关闭管道的输出文件描述符
    	waitpid(pid_child[i], NULL, 0);				//父进程等待前台进程的运行
    	i++;
    }
   if((pid_child[i]=fork())<0)
	{
		perror("Fork failed");
		exit(errno);
	}
    if(!(pid_child[i]=fork()))					//创建子进程
    {
	close(pipe_fd[i-1][1]);					//关闭进程的标准输入文件
        dup2(pipe_fd[i-1][0], 0); 				//将管道的读描述符复制到进程的标准输入
        close(pipe_fd[i-1][0]); 				//关闭进程的标准输出文件
	if(pipeCmd[i]->output != NULL)				//存在输出重定向
	{ 			
                if((pipeOut = open(pipeCmd[i]->output, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
		{
                    printf("打不开文件 %s！\n", pipeCmd[i]->output);
                    return ;
                }
                if(dup2(pipeOut, 1) == -1)
		{
                    printf("输出重定向错误！\n");
                    return;
                }
        }
        if(exists(pipeCmd[i]->args[0]))				//命令存在
	{ 
            if(execve(cmdBuff, pipeCmd[i]->args,NULL) < 0)	//执行命令
	    { 
                printf("execv failed!\n");
                return;
            }
   	}
    }
    close(pipe_fd[i-1][0]);					//关闭进程的标准输入文件
    waitpid(pid_child[i], NULL, 0); 				//父进程等待前台进程的运行
    return;
}

/************************第二类指令：执行外部命令********************************/
void execOuterCmd(SimpleCmd *cmd)
{
    pid_t pid;
    int pipeIn, pipeOut, i;
    if(exists(cmd->args[0]))	 			//命令存在		
    {
        if((pid = fork()) < 0)
	{
            perror("fork failed");
            return;
        }
        if(pid == 0)					//子进程
	{ 		
            kill(getppid(), SIGUSR1);
            if(cmd->input != NULL)			//存在输入重定向
            { 
                if((pipeIn = open(cmd->input, O_RDONLY, S_IRUSR|S_IWUSR)) == -1)
		{
                    printf("can't open the file %s！\n", cmd->input);
                    return;
                }
                if(dup2(pipeIn, 0) == -1)
		{
                    printf("Redirect standard input error！\n");
                    return;
                }
            }	
            if(cmd->output != NULL)			//存在输出重定向
	    { 
                if((pipeOut = open(cmd->output, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
		{
                    printf("can't open the file %s！\n", cmd->output);
                    return ;
                }
                if(dup2(pipeOut, 1) == -1)
		{
                    printf("Redirect standard output error！\n");
                    return;
                }
            }
            if(cmd->isBack)				//若是后台运行命令，等待父进程增加作业
	     { 
                signal(SIGUSR1, setGoon); 		//收到信号，setGoon函数将goon置1，以跳出下面的循环
                while(goon == 0) ; 			//等待父进程SIGUSR1信号，表示作业已加到链表中
                goon = 0; 				//置0，为下一命令做准备
                printf("[%d]\t%s\t\t%s\n", getpid(), RUNNING, inputBuff);
                kill(getppid(), SIGUSR1);
            }
            justArgs(cmd->args[0]);
            if(execv(cmdBuff, cmd->args) < 0)		 //执行命令
	    {
                printf("execv failed!\n");
                return;
            }
        }
	else						 //父进程
	{
		signal(SIGUSR1, setGoon);
		while (goon == 0) ;
		goon = 0;
            if(cmd ->isBack)				//后台命令    
	    {          
                fgPid = 0; 				//pid置0，为下一命令做准备
                addJob(pid); 				//增加新的作业
                kill(pid, SIGUSR1); 			//向子进程发信号，表示作业已加入
                signal(SIGUSR1, setGoon);		//等待子进程输出
                while(goon == 0) ;
                goon = 0;
            }else					//非后台命令
	    { 
                fgPid = pid;
                waitpid(pid, NULL, 0);
            }
	}
    }
    else 						//命令不存在
        printf("can't find the command %s !\n", inputBuff);
}

/**********************第三类命令：执行命令******************************/
void execSimpleCmd(SimpleCmd *cmd){
    int i, pid;
    char *temp;
    Job *now = NULL;
    
    if(strcmp(cmd->args[0], "exit") == 0)			//exit命令
        exit(0);
    else if (strcmp(cmd->args[0], "history") == 0)		//history命令
	{ 
		if(history.end == -1)
		{
			printf("not implement any command!\n");
			return;
        	}
       		i = history.start;
        	do {
           		 printf("%s\n", history.cmds[i]);
            		i= (i + 1)%HISTORY_LEN;
        	  } while(i != (history.end + 1)%HISTORY_LEN);
	}
    else if (strcmp(cmd->args[0], "jobs") == 0)		//jobs命令
	{ 
        if(head == NULL)
            printf("not any task!\n");
	else 
	{
            printf("index\tpid\tstate\t\tcommand\n");
            for(i = 1, now = head; now != NULL; now = now->next, i++)
                printf("%d\t%d\t%s\t\t%s\n", i, now->pid, now->state, now->cmd);
        }
    }
    else if (strcmp(cmd->args[0], "cd") == 0)		//cd命令
	{ 
		temp = cmd->args[1];
        	if(temp != NULL)
		{
	            	if(chdir(temp) < 0)
	                printf("cd; %s filename or document name error！\n", temp);
        	}
	}
    else if (strcmp(cmd->args[0], "fg") == 0)		//fg命令
	{
       		temp = cmd->args[1];
        	if(temp != NULL && temp[0] == '%')
		{
			pid = str2Pid(temp, 1, strlen(temp));
            		if(pid != -1)
                		fg_exec(pid);
        	}
		else
            		printf("fg; parameters not legal，correct for：fg %<int>\n");
	}
   else if (strcmp(cmd->args[0], "bg") == 0)		//bg命令
	{ 
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%')
	{
            pid = str2Pid(temp, 1, strlen(temp));            
            if(pid != -1)
                bg_exec(pid);
        }
	else
            printf("bg; parameters not legal，correct for：bg %<int>\n");
	}
   else				//外部命令
        execOuterCmd(cmd);
    
    //释放结构体空间
    for(i = 0; cmd->args[i] != NULL; i++)
    {
        free(cmd->args[i]);
        free(cmd->input);
        free(cmd->output);
    }
}


/*******************************************************
                     命令执行接口
********************************************************/
void execute(){
    int i,j,pipeNum,pipe[128];			//pipeNum记录管道指令数，
    SimpleCmd *pipeCmd[128];			//记录可能的用管道连接的指令
    i=0;
    pipeNum=0;					//全部初始化
    while(i<strlen(inputBuff))
    {
	if(inputBuff[i]=='|')
		pipe[pipeNum++]=i;		//记下当前出现|的位置
	i++;
    }
    pipeNum++;			//总共的管道指令数是'|'+1;
    i=0;
    j=0;
    if(pipeNum>1)		//说明有管道指令
    {
	while(j<pipeNum-1)
	{
		SimpleCmd *cmd=handleSimpleCmdStr(i,pipe[j]-1);		//解析当前指令
		pipeCmd[j]=cmd;						//累加到pipeCmd中
		i=pipe[j]+1;						//从出现"|"的下一个地方开始就是下一条指令
		j++;
	}
	SimpleCmd *cmd = handleSimpleCmdStr(i,strlen(inputBuff));	//i处保留的就是最后一条指令的起始位置
	pipeCmd[j]=cmd;							//存储到pipeCmd中
	ExecPipe(pipeCmd,pipeNum);					//执行管道指令
    }
    else
    {
		SimpleCmd *cmd = handleSimpleCmdStr(0,strlen(inputBuff));
		execSimpleCmd(cmd);					//执行命令
    }
}
