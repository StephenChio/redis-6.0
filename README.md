# Redis

想要学习Redis工作方式，最好的方式就是了解它的线程模型，它究竟是如何接受请求，如何处理请求，它究竟是传说中的单线程工作还是多线程，让我们来一探究竟。

### main()

redis启动函数是server.c文件的最后一个函数

```c
*主程序入口*/
int main(int argc, char **argv)
{
    struct timeval tv;
    int j;

#ifdef REDIS_TEST
    if (argc == 3 && !strcasecmp(argv[1], "test"))
    {
        if (!strcasecmp(argv[2], "ziplist"))
        {
            return ziplistTest(argc, argv);
        }
        else if (!strcasecmp(argv[2], "quicklist"))
        {
            quicklistTest(argc, argv);
        }
        else if (!strcasecmp(argv[2], "intset"))
        {
            return intsetTest(argc, argv);
        }
        else if (!strcasecmp(argv[2], "zipmap"))
        {
            return zipmapTest(argc, argv);
        }
        else if (!strcasecmp(argv[2], "sha1test"))
        {
            return sha1Test(argc, argv);
        }
        else if (!strcasecmp(argv[2], "util"))
        {
            return utilTest(argc, argv);
        }
        else if (!strcasecmp(argv[2], "endianconv"))
        {
            return endianconvTest(argc, argv);
        }
        else if (!strcasecmp(argv[2], "crc64"))
        {
            return crc64Test(argc, argv);
        }
        else if (!strcasecmp(argv[2], "zmalloc"))
        {
            return zmalloc_test(argc, argv);
        }

        return -1; /* test not found */
    }
#endif

    /* We need to initialize our libraries, and the server configuration.
    我们需要初始化我们的库，和服务器的配置*/
#ifdef INIT_SETPROCTITLE_REPLACEMENT
    spt_init(argc, argv);
#endif
    setlocale(LC_COLLATE, "");
    tzset(); /* Populates 'timezone' global. */
    //设置内存溢出处理器
    zmalloc_set_oom_handler(redisOutOfMemoryHandler);
    srand(time(NULL) ^ getpid());
    gettimeofday(&tv, NULL);
    init_genrand64(((long long)tv.tv_sec * 1000000 + tv.tv_usec) ^ getpid());
    crc64_init();

    /* Store umask value. Because umask(2) only offers a set-and-get API we have
     * to reset it and restore it back. We do this early to avoid a potential
     * race condition with threads that could be creating files or directories.
    设置文件掩码值，赋予程序对文件的最高操作权限 */
    umask(server.umask = umask(0777));

    uint8_t hashseed[16];
    //初始化一个随机hash种子
    getRandomBytes(hashseed, sizeof(hashseed));
    dictSetHashFunctionSeed(hashseed);
    //检查是否是哨兵模式：如果参数中有 --sentinel 或 argv[0] 包含“redis-sentinel”，则返回 1。
    server.sentinel_mode = checkForSentinelMode(argc, argv);
    // 为 `server` 数据结构设置默认值，设置配置默认值（没有配置文件的情况下的默认值）
    initServerConfig();
    ACLInit(); /* The ACL subsystem must be initialized ASAP because the
                  basic networking code and client creation depends on it.
                  ACL安全策略子系统必须尽可能快的初始化，因为基础网络代码和客户端创建都需要用到 */
    //初始化模块系统
    moduleInitModulesSystem();
    tlsInit();

    /* Store the executable path and arguments in a safe place in order
     * to be able to restart the server later.
       把可执行路径和参数保存在一个安全的地方以便于去稍后重新启动服务器
     */
    server.executable = getAbsolutePath(argv[0]);
    server.exec_argv = zmalloc(sizeof(char *) * (argc + 1));
    server.exec_argv[argc] = NULL;
    for (j = 0; j < argc; j++)
    {
        server.exec_argv[j] = zstrdup(argv[j]);
    }
    /* We need to init sentinel right now as parsing the configuration file
     * in sentinel mode will have the effect of populating the sentinel
     * data structures with master nodes to monitor.
     * 如果我们在哨兵模式下，我们还需要去初始化哨兵配置和哨兵*/
    if (server.sentinel_mode)
    {
        initSentinelConfig();
        initSentinel();
    }

    /* Check if we need to start in redis-check-rdb/aof mode. We just execute
     * the program main. However the program is part of the Redis executable
     * so that we can easily execute an RDB check on loading errors.
     * 检查是否我们需要在redis-check-rdb/aof模式下启动，我们只是执行项目主程序，
     * 但是该程序是Redis可执行程序的一部分，所以我们可以很容易在加载错误的情况下执行一个RDB检查
     *  */
    if (strstr(argv[0], "redis-check-rdb") != NULL)
        redis_check_rdb_main(argc, argv, NULL);
    else if (strstr(argv[0], "redis-check-aof") != NULL)
        redis_check_aof_main(argc, argv);

    //当参数数量大于等于2时候，说明至少有一个输入参数（排除命令本身）
    if (argc >= 2)
    {
        j = 1; /* First option to parse in argv[] */
        sds options = sdsempty();
        char *configfile = NULL;

        /* Handle special options --help and --version 处理特殊选项，help和version*/
        if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0)
            version();
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0)
            usage();
        if (strcmp(argv[1], "--test-memory") == 0)
        {
            if (argc == 3)
            {
                memtest(atoi(argv[2]), 50);
                exit(0);
            }
            else
            {
                fprintf(stderr, "Please specify the amount of memory to test in megabytes.\n");
                fprintf(stderr, "Example: ./redis-server --test-memory 4096\n\n");
                exit(1);
            }
        }

        /* First argument is the config file name? 第一个参数是否配置文件名称？*/
        if (argv[j][0] != '-' || argv[j][1] != '-')
        {
            //如果第一个参数不以-或者--开头，那么我们认为它是配置文件的路径
            configfile = argv[j];
            server.configfile = getAbsolutePath(configfile);
            /* Replace the config file in server.exec_argv with
             * its absolute path.
             * 把上面保存的对应参数替换成文件的绝对路径
             */
            zfree(server.exec_argv[j]);
            server.exec_argv[j] = zstrdup(server.configfile);
            j++;
        }

        /* All the other options are parsed and conceptually appended to the
         * configuration file. For instance --port 6380 will generate the
         * string "port 6380\n" to be parsed after the actual file name
         * is parsed, if any.
         * 其他输入的选项也会被解析追加到配置文件，
         * 例如 --port 6380 会生成"port 6380\n" 字符串追加到真实解析的配置文件上
         *
         * */
        while (j != argc)
        {
            if (argv[j][0] == '-' && argv[j][1] == '-')
            {
                /* Option name 如果是以--开头的参数，去除--添加到options*/
                if (!strcmp(argv[j], "--check-rdb"))
                {
                    /* Argument has no options, need to skip for parsing.
                    这个字段没有参数，需要跳过下一个进行解析 */
                    j++;
                    continue;
                }
                if (sdslen(options))
                {
                    options = sdscat(options, "\n");
                }
                options = sdscat(options, argv[j] + 2);
                options = sdscat(options, " ");
            }
            else
            {
                /* Option argument 如果 不是以--开头的参数，直接添加到 options */
                options = sdscatrepr(options, argv[j], strlen(argv[j]));
                options = sdscat(options, " ");
            }
            j++;
        }
        if (server.sentinel_mode && configfile && *configfile == '-')
        {
            //实例是哨兵模式，不允许通过命令行指定配置文件运行
            serverLog(LL_WARNING, "Sentinel config from STDIN not allowed.");
            serverLog(LL_WARNING, "Sentinel needs config file on disk to save state.  Exiting...");
            exit(1);
        }
        //当我们进入到这里的时候，说明我们配置了一些参数，
        //我们把之前设置的RDB更新策略重置，后面可以根据您给的配置或者默认配置进行再次设置。
        resetServerSaveParams();
        //加载配置文件，如果configfile为空则加载默认配置，options是命令行输入的配置，优先级高于配置文件中的设置
        // options会把配置文件中同样的项覆盖
        loadServerConfig(configfile, options);
        sdsfree(options);
    }
    //判断是否是监视者模式
    server.supervised = redisIsSupervised(server.supervised_mode);
    //如果不是监视者模式，而且开启了后台运行，那么我们开启守护进程
    int background = server.daemonize && !server.supervised;
    if (background)
        daemonize();

    serverLog(LL_WARNING, "oO0OoO0OoO0Oo Redis is starting oO0OoO0OoO0Oo");
    serverLog(LL_WARNING,
              "Redis version=%s, bits=%d, commit=%s, modified=%d, pid=%d, just started",
              REDIS_VERSION,
              (sizeof(long) == 8) ? 64 : 32,
              redisGitSHA1(),
              strtol(redisGitDirty(), NULL, 10) > 0,
              (int)getpid());

    if (argc == 1)
    {
        //如果没有配置任何参数，那么配置文件就是使用默认配置文件
        serverLog(LL_WARNING, "Warning: no config file specified, using the default config. In order to specify a config file use %s /path/to/%s.conf", argv[0], server.sentinel_mode ? "sentinel" : "redis");
    }
    else
    {
        serverLog(LL_WARNING, "Configuration loaded");
    }
    // 此功能可使Redis根据其进程主动控制其所有进程的oom_score_adj值
    // oom_score_adj 的取值范围是 -1000～1000,如果设置成-1000，操作系统将不会kill该程序
    readOOMScoreAdj();
    //初始化服务器一些配置，以及创建事件循环
    initServer();
    if (background || server.pidfile)
        createPidFile();
    redisSetProcTitle(argv[0]);
    //打印Ascii图标
    redisAsciiArt();
    //根据 /proc/sys/net/core/somaxconn 的值，检查 server.tcp_backlog 是否可以在 Linux 中实际执行，或者警告它。
    /**
     * 　　对于一个TCP连接，Server与Client需要通过三次握手来建立网络连接.当三次握手成功后,
     * 我们可以看到端口的状态由LISTEN转变为ESTABLISHED,接着这条链路上就可以开始传送数据了.
     * 每一个处于监听(Listen)状态的端口,都有自己的监听队列.监听队列的长度,与如下两方面有关:
     * - somaxconn参数.
     * - 使用该端口的程序中listen()函数.
     * 
     * 关于somaxconn参数:
     * 定义了系统中每一个端口最大的监听队列的长度,这是个全局的参数,默认值为128.
     * 限制了每个端口接收新tcp连接侦听队列的大小。
     * 对于一个经常处理新连接的高负载 web服务环境来说，默认的 128 太小了。
     * 大多数环境这个值建议增加到 1024 或者更多。 
     * 服务进程会自己限制侦听队列的大小(例如 sendmail(8) 或者 Apache)，
     * 常常在它们的配置文件中有设置队列大小的选项。大的侦听队列对防止拒绝服务 DoS 攻击也会有所帮助。
     */
    checkTcpBacklogSettings();
    //实例是否是哨兵模式
    if (!server.sentinel_mode)
    {
        //非哨兵模式
        /* Things not needed when running in Sentinel mode. */
        serverLog(LL_WARNING, "Server initialized");
#ifdef __linux__
        /**
         * 
         * 判断 vm.overcommit_memory 是否为1或者2，如果为0 打印警告日志
         * 0：表示内核将检查是否有足够的可用内存供应用进程使用；如果有足够的可用内存，内存申请允许；否则，内存申请失败，并把错误返回给应用进程。
         * 1：表示内核允许分配所有的物理内存，而不管当前的内存状态如何。
         * 2：表示内核允许分配超过所有物理内存和交换空间总和的内存。
         * 
         */
        linuxMemoryWarnings();
#if defined(__arm64__)
        int ret;
        /**
         * 旧的arm64 Linux内核有一个bug，在某些情况下，在后台保存期间可能会导致数据损坏。
         * 此函数检查内核是否受到影响。测试失败时返回-1，如果内核似乎受到影响，则返回1，否则返回
         * 
         */
        if ((ret = linuxMadvFreeForkBugCheck()))
        {
            if (ret == 1)
                serverLog(LL_WARNING, "WARNING Your kernel has a bug that could lead to data corruption during background save. "
                                      "Please upgrade to the latest stable kernel.");
            else
                serverLog(LL_WARNING, "Failed to test the kernel for a bug that could lead to data corruption during background save. "
                                      "Your system could be affected, please report this error.");
            if (!checkIgnoreWarning("ARM64-COW-BUG"))
            {
                serverLog(LL_WARNING, "Redis will now exit to prevent data corruption. "
                                      "Note that it is possible to suppress this warning by setting the following config: ignore-warnings ARM64-COW-BUG");
                exit(1);
            }
        }
#endif /* __arm64__ */
#endif /* __linux__ */
        //加载服务器中的所有模块
        moduleLoadFromQueue();
        //配置ACL Access Control List（访问权限控制列表）（不能同时在配置文件和ACL文件配置ACL设置，否则可能出现漏洞）
        ACLLoadUsersAtStartup();
        //有一些步骤需要在初始化之后在进行完成（在模块加载之后）
        InitServerLast();
        //启动时调用的函数，用于在内存中加载 RDB 或 AOF 文件。
        loadDataFromDisk();
        if (server.cluster_enabled)
        {
            if (verifyClusterConfigWithData() == C_ERR)
            {
                serverLog(LL_WARNING,
                          "You can't have keys in a DB different than DB 0 when in "
                          "Cluster mode. Exiting.");
                exit(1);
            }
        }
        if (server.ipfd_count > 0 || server.tlsfd_count > 0)
            serverLog(LL_NOTICE, "Ready to accept connections");
        if (server.sofd > 0)
            serverLog(LL_NOTICE, "The server is now ready to accept connections at %s", server.unixsocket);
        if (server.supervised_mode == SUPERVISED_SYSTEMD)
        {
            if (!server.masterhost)
            {
                redisCommunicateSystemd("STATUS=Ready to accept connections\n");
                redisCommunicateSystemd("READY=1\n");
            }
            else
            {
                redisCommunicateSystemd("STATUS=Waiting for MASTER <-> REPLICA sync\n");
            }
        }
    }
    else
    { 
        InitServerLast();
        sentinelIsRunning();
        if (server.supervised_mode == SUPERVISED_SYSTEMD)
        {
            redisCommunicateSystemd("STATUS=Ready to accept connections\n");
            redisCommunicateSystemd("READY=1\n");
        }
    }

    /* Warning the user about suspicious maxmemory setting. 警告用户有关可疑的最大内存设置。小于1MB*/
    if (server.maxmemory > 0 && server.maxmemory < 1024 * 1024)
    {
        serverLog(LL_WARNING, "WARNING: You specified a maxmemory value that is less than 1MB (current value is %llu bytes). Are you sure this is what you really want?", server.maxmemory);
    }

    redisSetCpuAffinity(server.server_cpulist);
    //此函数将根据用户指定的配置配置当前进程的oom_score_adj。这目前仅在Linux上实现。（取值-1000-1000）-1000代表永远不会被kill
    //process_class值为-1表示OOM_CONFIG_MASTER或OOM_CONFIG_REPLICA，具体取决于当前角色。
    setOOMScoreAdj(-1);

    //开启主事件循环，处理数据
    aeMain(server.el);
    //释放所有事件循环
    aeDeleteEventLoop(server.el);
    return 0;
}

```

从代码中有几个重要的点需要注意：

1.redis启动命令的第二个参数，如果不以-或者--开头，redis默认认为是配置文件参数，那么它就会通过该参数寻找配置文件，如果出错，则程序会启动失败

2.redis并没有去默认文件路径寻找配置文件，它的默认配置是写到了redis代码里的，如果你没有指定配置文件，那么它只会根据代码内容初始化配置文件。所谓的默认配置文件，只是它的内容和redis代码里的默认配置是一致的，仅此而已。

3.redis 6.0之后支持了多线程I/O处理客户的信息，但是默认情况下是不开启的，（可能是为了兼容之前运行Redis的单核或者双核CPU服务器）如果需要开启，则需要指定配置文件运行，并且做了相应的配置。在4核cpu以上的机器上，官方都建议我们开启多线程I/O，可以有效提升性能。

4.Redis在linux机器上建议我们去使用vm.overcommit_memory=1这个配置。意味着"表示内核允许分配所有的物理内存，而不管当前的内存状态如何"，对于Redis这个内存数据库来说，这是它愿意去看到的，但是我们在生产环节上也遇到过一些问题：在运行了k8s集群的机器上（需要强制linux系统关闭 swap（内存交换）来提升性能，避免频繁与硬盘进行内存交换），如果还运行了Redis，那么在高负荷的工作条件下，可能会把物理机器的内存耗尽到不可恢复的状态。



### 初始化默认配置

```c
void initConfigValues() {
​    for (standardConfig *config = configs; config->name != NULL; config++) {
​        config->interface.init(config->data);
​    }
}
```

Redis通过调用init函数指针来为每一个不同数据类型的配置进行初始化，它里面是通过宏替换进行数据结构初始化，过程比较复杂就不展开说了，也没有特别难懂，可以亲自到源代码感受一下。



### 绑定地址和端口

Redis绑定地址和端口的逻辑在main函数中的initServer函数里。当绑定地址和端口失败则退出。

```c
 /* Open the TCP listening socket for the user commands. 为用户命令打开TCP监听socket */

​    if (server.port != 0 && listenToPort(server.port, server.ipfd, &server.ipfd_count) == C_ERR)
         exit(1);
```

具体的逻辑我们继续看listenToPort函数

```c
/* Initialize a set of file descriptors to listen to the specified 'port'
 * binding the addresses specified in the Redis server configuration.
 * 初始化一组文件描述符以监听绑定在 Redis 服务器配置中指定的地址的指定端口。
 * The listening file descriptors are stored in the integer array 'fds'
 * and their number is set in '*count'.
 * 监听的文件描述符被存储在fds整型数组上和它们的数量被保存在*count上
 * The addresses to bind are specified in the global server.bindaddr array
 * and their number is server.bindaddr_count. If the server configuration
 * contains no specific addresses to bind, this function will try to
 * bind * (all addresses) for both the IPv4 and IPv6 protocols.
 *
 * 要绑定的地址在全局 server.bindaddr 数组中指定，它们的数量是 server.bindaddr_count。
 * 如果服务器配置不包含要绑定的特定地址，此函数将尝试为 IPv4 和 IPv6 协议绑定 *（所有地址）。
 * 
 * On success the function returns C_OK.
 * 成功时，函数返回 C_OK。
 *
 * On error the function returns C_ERR. For the function to be on
 * error, at least one of the server.bindaddr addresses was
 * impossible to bind, or no bind addresses were specified in the server
 * configuration but the function is not able to bind * for at least
 * one of the IPv4 or IPv6 protocols. 
 * 出错时，函数返回 C_ERR。
 * 要使函数出错，至少有一个 server.bindaddr 地址无法绑定，
 * 或者在服务器配置中未指定绑定地址，但函数无法绑定 * IPv4 或 IPv6 协议中的至少一个。*/
int listenToPort(int port, int *fds, int *count)
{
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0.
     如果没有指定绑定地址那么默认强制绑定0.0.0.0 ，如果j==0，总是会进入循环
     */
    if (server.bindaddr_count == 0)
        server.bindaddr[0] = NULL;
    for (j = 0; j < server.bindaddr_count || j == 0; j++)
    {
        if (server.bindaddr[j] == NULL)
        {
            int unsupported = 0;
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0.
             为 IPv6 和 IPv4 绑定 *，仅当 server.bindaddr_count == 0 时才会进入这里*/
            fds[*count] = anetTcp6Server(server.neterr, port, NULL,
                                         server.tcp_backlog);
            if (fds[*count] != ANET_ERR)
            {
                anetNonBlock(NULL, fds[*count]);
                (*count)++;
            }
            else if (errno == EAFNOSUPPORT)
            {
                unsupported++;
                serverLog(LL_WARNING, "Not listening to IPv6: unsupported");
            }

            if (*count == 1 || unsupported)
            {
                /* Bind the IPv4 address as well. */
                fds[*count] = anetTcpServer(server.neterr, port, NULL,
                                            server.tcp_backlog);
                if (fds[*count] != ANET_ERR)
                {
                    anetNonBlock(NULL, fds[*count]);
                    (*count)++;
                }
                else if (errno == EAFNOSUPPORT)
                {
                    unsupported++;
                    serverLog(LL_WARNING, "Not listening to IPv4: unsupported");
                }
            }
            /* Exit the loop if we were able to bind * on IPv4 and IPv6,
             * otherwise fds[*count] will be ANET_ERR and we'll print an
             * error and return to the caller with an error.
             * 如果我们成功绑定了IPv4和IPv6，会退出循环
             * 否则fds[*count]会等于ANET_ERR,我们会打印错误和返回错误
             * */
            if (*count + unsupported == 2)
                break;
        }
        else if (strchr(server.bindaddr[j], ':'))
        {
            /* Bind IPv6 address. */
            fds[*count] = anetTcp6Server(server.neterr, port, server.bindaddr[j],
                                         server.tcp_backlog);
        }
        else
        {
            /* Bind IPv4 address. */
            fds[*count] = anetTcpServer(server.neterr, port, server.bindaddr[j],
                                        server.tcp_backlog);
        }
        if (fds[*count] == ANET_ERR)
        {
            int net_errno = errno;
            serverLog(LL_WARNING,
                      "Could not create server TCP listening socket %s:%d: %s",
                      server.bindaddr[j] ? server.bindaddr[j] : "*",
                      port, server.neterr);
            if (net_errno == ENOPROTOOPT || net_errno == EPROTONOSUPPORT ||
                net_errno == ESOCKTNOSUPPORT || net_errno == EPFNOSUPPORT ||
                net_errno == EAFNOSUPPORT || net_errno == EADDRNOTAVAIL)
                continue;
            return C_ERR;
        }
        anetNonBlock(NULL, fds[*count]);
        (*count)++;
    }
    return C_OK;
}

```

此函数就是通过调用anetTcpServer和anetTcp6Server分别为IPv4地址和IPv6地址绑定端口，并返回一个文件描述符如何通过anetNonBlock来设置当前套接字为非阻塞，那么socket在收到消息的时候不会一直阻塞，而是会马上通知程序进行读取，实现的方式通常有select或者epoll（linux）。

该函数会帮每一个你在配置文件中配置的地址绑定端口，例如：如果您设置了 127.0.0.1 ，192.168.31.31 和 101.122.102.233（Redis默认可以让我们最多配置16个不同的地址） 作为您需要bind的地址（前提是三个地址都是可以访问本机的地址，如果您没有公网地址，但是配置了公网地址，那么就会启动失败，或者您输入了跟你不相符的内网地址也是一样的。），那么它会为每一个地址跟端口绑定生成一个socket，返回相应数量的文件描述符。当然您也可以设置成IPv6的格式。

在bind配置下，只有访问您设置的地址才能访问Redis，如果您设置成127.0.0.1，那么外部的机器都不能访问您的Redis服务器（那么您会是安全的），因为所有访问127.0.0.1的机器最终只会访问它自己本地，（众所周知127.0.0.1是一个回环地址），如果您设置成0.0.0.0，或者不设置bind属性，那么Redis会为您所有属于您的地址绑定端口并提供外部访问，这样造成的结果就是知道您任意一个ip地址和正确的redis端口，都有访问成功的可能性（前提是知道密码，默认是无密码的）

我们看完了redis是如何生成socket等待外部连接的，那么我们就来看看，客户的是如何连接redis的。

### 打开TCP监听socket

当我们初始化完成socket之后，我们就需要为每一个socket创建一个事件循环，来获取来自socket的数据。

具体的代码在initServer中

```c
* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets.
     * 创建一个事件处理程序以接受 TCP 和 Unix 域套接字中的新连接
     * 每绑定一个地址就创建一个事件处理器
     */
    for (j = 0; j < server.ipfd_count; j++)
    {
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE, acceptTcpHandler, NULL) == AE_ERR)
        {
            serverPanic(
                "Unrecoverable error creating server.ipfd file event.");
        }
    }
```

里面的核心实现分别是创建事件处理aeCreateFileEvent和实际的处理入口acceptTcpHandler。

让我们先看aeCreateFileEvent

```c
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    aeFileEvent *fe = &eventLoop->events[fd];

    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
    fe->clientData = clientData;
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;
    return AE_OK;
}
```

创建事件循环的代码不长，参数中的mask位的值，从上面可知是AE_READABLE，代表该实现循环会等到该socket存在可读数据的时候调用acceptTcpHandler进行处理

我们再看看acceptTcpHandler

```c
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd, max = MAX_ACCEPTS_PER_CALL;
    char cip[NET_IP_STR_LEN];
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);
    //一次调用最多接收1000个TCP连接
    while(max--) {
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                serverLog(LL_WARNING,
                    "Accepting client connection: %s", server.neterr);
            return;
        }
        printf("Accepted cfd:%d fd:%d addr:%s:%d\n",cfd,fd, cip, cport);
        serverLog(LL_VERBOSE,"Accepted %s:%d", cip, cport);
        acceptCommonHandler(connCreateAcceptedSocket(cfd),0,cip);
    }
}
```

acceptTcpHandler顾名思义就是接受TCP请求的时候会触发的函数，像是在客户端请求连接的时候，一旦完成三次握手成功建立连接之后，此客户端以后的发送的数据都不会触发该函数，即使它们的数据也是通过TCP进行发送的，因为他们所利用的文件描述符是不相同的。

此函数的主要内容就是收到有TCP连接请求的时候进行处理，最多一次处理1000个TCP请求。

fd为侦听地址的文件描述符，也就是一般情况下用于处理连接的socket，但是它并不是TCP通信之间发送数据的socket。我们必须为连接的每一个客户端创建一个socket，尽管他们都是通过6379端口（如果您没有修改默认端口）进入到Redis程序的，但是他们还是不一样的。

我们看到anetTcpAccept函数里的更深层次的实现

```c
static int anetGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len) {
    int fd;
    while(1) {
        fd = accept(s,sa,len);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            else {
                anetSetError(err, "accept: %s", strerror(errno));
                return ANET_ERR;
            }
        }
        break;
    }
    return fd;
}
```

我们调用accept函数来正式创建TCP一个连接，并为该连接初始化socket，接受数据，并获取该文件描述符fd。

可能难以理解的点：一开始初始化程序绑定地址端口创建的描述符所对应的socket并不是一个TCP连接，正如我们都所厌恶的三次握手所描述的那样，一个TCP连接由客户端发起，并且服务端接受，这样一来二去我们开始建立连接，但是Redis程序启动的时候，只是声明了6379端口的占用，也生成了相对应的socket来接受连接，此时并没有和客户端之间的互动，由此可见，一开始所标识的文件描述符对应的并不是一个TCP连接，它只是用于监听的一个作用。当完成了TCP连接之后，我们从epoll程序（linux上常用）得知我们收到了一些内容，我们尝试去调用accept的时候，我们所获取到的文件描述符才是真正用于Redis TCP收发信息的socket。也就是acceptTcpHandler函数中的cfd。

接下来我们需要看看Redis程序对cfd对应的连接做了什么。

```c
connection *connCreateSocket() {
    connection *conn = zcalloc(sizeof(connection));
    conn->type = &CT_Socket;
    conn->fd = -1;

    return conn;
}

/* Create a new socket-type connection that is already associated with
 * an accepted connection.
 *
 * The socket is not ready for I/O until connAccept() was called and
 * invoked the connection-level accept handler.
 *
 * Callers should use connGetState() and verify the created connection
 * is not in an error state (which is not possible for a socket connection,
 * but could but possible with other protocols).
 */
connection *connCreateAcceptedSocket(int fd) {
    connection *conn = connCreateSocket();
    conn->fd = fd;
    conn->state = CONN_STATE_ACCEPTING;
    return conn;
}
```

获取到cfd文件描述符之后第一个对它进行操作的是connCreateAcceptedSocket函数，connCreateAcceptedSocket函数的内容非常简单，只是初始化了Reids中的connection数据结构，把conn的fd值设置成cfd的值。也就是为这个文件描述符也就是TCP连接建立了一个Redis对象，然后进行返回。

然后就是acceptCommonHandler对所创建的对象的操作。

```c
static void acceptCommonHandler(connection *conn, int flags, char *ip) {
    client *c;
    char conninfo[100];
    UNUSED(ip);

    //判断连接状态是否是CONN_STATE_ACCEPTING
 
    //判断客户端数量是否超出限制

    /* Create connection and client 创建一个连接和客户端 */
    if ((c = createClient(conn)) == NULL) {
        serverLog(LL_WARNING,
            "Error registering fd event for the new client: %s (conn: %s)",
            connGetLastError(conn),
            connGetInfo(conn, conninfo, sizeof(conninfo)));
        connClose(conn); /* May be already closed, just ignore errors */
        return;
    }

    /* Last chance to keep flags */
    c->flags |= flags;

    /* Initiate accept.
     *
     * Note that connAccept() is free to do two things here:
     * 1. Call clientAcceptHandler() immediately;
     * 2. Schedule a future call to clientAcceptHandler().
     *
     * Because of that, we must do nothing else afterwards.
     */
    if (connAccept(conn, clientAcceptHandler) == C_ERR) {
        char conninfo[100];
        if (connGetState(conn) == CONN_STATE_ERROR)
            serverLog(LL_WARNING,
                    "Error accepting a client connection: %s (conn: %s)",
                    connGetLastError(conn), connGetInfo(conn, conninfo, sizeof(conninfo)));
        freeClient(connGetPrivateData(conn));
        return;
    }
}
```

acceptCommonHandler的主要操作就是为connection创建了一个Redis client数据结构，并且把connection的状态修改成CONN_STATE_CONNECTED（在函数connSocketAccept中实现，通过上面connAccept(conn, clientAcceptHandler)代码进入）和 调用clientAcceptHandler函数更新了一些统计信息。

至此我们的一个客户端已经成功连接上Redis服务器了，我们也获取到了该TCP通道的文件描述符，创建了一些实例对象，通过epoll机制我们可以有效的知道什么时候收到客户端发来的消息，那么我们到底是如何接受客户端的命令的，我们接着分析。

在上面代码中，有一行c = createClient(conn)) == NULL，做的内容如下：

```c
client *createClient(connection *conn) {
    client *c = zmalloc(sizeof(client));

    /* passing NULL as conn it is possible to create a non connected client.
     * This is useful since all the commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */
    if (conn) {
        connNonBlock(conn);
        connEnableTcpNoDelay(conn);
        if (server.tcpkeepalive)
            connKeepAlive(conn,server.tcpkeepalive);
        connSetReadHandler(conn, readQueryFromClient);
        connSetPrivateData(conn, c);
    }

    selectDb(c,0);
    uint64_t client_id = ++server.next_client_id;
    //省略 赋值操作
    /* If the default user does not require authentication, the user is
     * directly authenticated. */
    //省略 赋值操作
    listSetFreeMethod(c->reply,freeClientReplyValue);
    listSetDupMethod(c->reply,dupClientReplyValue);
    //省略 赋值操作
    listSetFreeMethod(c->pubsub_patterns,decrRefCountVoid);
    listSetMatchMethod(c->pubsub_patterns,listMatchObjects);
    if (conn) linkClient(c);
    initClientMultiState(c);
    return c;
}

```

我们在上面省略了一下无关紧要的赋值操作，我们注意看if（conn）代码块下的内容，我们调用connSetReadHandler并为函数传入了conn和一个读处理函数readQueryFromClient，它的作用就是把为该conn的fd（文件描述符）添加一个读事件处理器，使用的方法还是通过aeCreateFileEvent，这在我们上面已经介绍过了，我们可以阅读下面代码。

```c
static inline int connSetReadHandler(connection *conn, ConnectionCallbackFunc func) {
    return conn->type->set_read_handler(conn, func);
}

static int connSocketSetReadHandler(connection *conn, ConnectionCallbackFunc func) {
    if (func == conn->read_handler) return C_OK;

    conn->read_handler = func;
    if (!conn->read_handler)
        aeDeleteFileEvent(server.el,conn->fd,AE_READABLE);
    else
        if (aeCreateFileEvent(server.el,conn->fd,
                    AE_READABLE,conn->type->ae_handler,conn) == AE_ERR) return C_ERR;
    return C_OK;
}
```

在先前的代码分析中，我们分析了aeCreateFileEvent把服务器监听的socket和acceptTcpHandler绑定起来，在socket收到数据的时候，会调用该sokcet文件描述符所绑定的读处理器，该处理器就是acceptTcpHandler，那我们建立完TCP连接之后，我们又重新生成了该TCP socket所对应的文件文件描述符，我们还没有为该描述符添加事件处理，只有添加了事件处理之后，从该socket收到数据之后，我们才能找到处理方法，该处理方法如代码所示，就是readQueryFromClient。

那么现在已经非常清晰了，从服务器监听端口（socket1）来的数据，我们认为它是来建立TCP连接请求的，所以我们调用acceptTcpHandler，当我们完成3次握手之后，通过accept函数传入socket1，并生成了socket2，也就是与客户端TCP连接的socket。从socket2来的数据，我们认为它是客户端发来的请求，所以我们调用readQueryFromClient。

至此我们已经分析完，Redis服务器是如何处理TCP连接请求和建立了请求之后为它分配的数据函数。我们下面就来看看readQueryFromClient函数它的庐山真面目。

```c
/**
 * @brief 从客户端读取数据
 * 
 * @param conn 
 */
void readQueryFromClient(connection *conn) {
    printf("readQueryFromClient\n");
    //从连接中获取客户端实例
    client *c = connGetPrivateData(conn);
    int nread, readlen;
    size_t qblen;

    /* Check if we want to read from the client later when exiting from
     * the event loop. This is the case if threaded I/O is enabled. 
     在退出事件循环时检查我们是否想稍后从客户端读取。如果启用了线程 I/O，就会出现这种情况。
     */
    if (postponeClientRead(c)) return;

    /* Update total number of reads on server 
    更新服务器处理的总数量 */
    server.stat_total_reads_processed++;
    //通用I/O 缓冲区大小 默认16kb
    readlen = PROTO_IOBUF_LEN;
    /* If this is a multi bulk request, and we are processing a bulk reply
     * that is large enough, try to maximize the probability that the query
     * buffer contains exactly the SDS string representing the object, even
     * at the risk of requiring more read(2) calls. This way the function
     * processMultiBulkBuffer() can avoid copying buffers to create the
     * Redis Object representing the argument. 
     * 如果这是一个多批量请求，并且我们正在处理一个足够大的批量回复，
     * 请尝试最大化查询缓冲区恰好包含表示对象的 SDS 字符串的概率，
     * 即使可能需要更多的 read() 调用 . 
     * 这样，函数 processMultiBulkBuffer() 可以避免复制缓冲区来创建表示参数的 Redis 对象。
     * */
    printf("c->reqtype:%d\n",c->reqtype);
    if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= PROTO_MBULK_BIG_ARG)
    {
        ssize_t remaining = (size_t)(c->bulklen+2)-sdslen(c->querybuf);

        /* Note that the 'remaining' variable may be zero in some edge case,
         * for example once we resume a blocked client after CLIENT PAUSE. 
         请注意，在某些情况下，“remaining”变量可能为零，例如：一旦我们在客户端暂停后恢复被阻塞的客户端*/
        if (remaining > 0 && remaining < readlen) {
            readlen = remaining;
        }
    }
    //获取已经读取的长度，作为偏移量加到下面connRead函数中，代表把读取的数据追加到后方
    qblen = sdslen(c->querybuf);
    if (c->querybuf_peak < qblen) {
        //更新querybuf 大小的最近（100 毫秒或更多）峰值
        c->querybuf_peak = qblen;
    }
    //我们扩大字符串的长度，以至于我们有足够的空间去读取新数据和存放结尾NULL符号
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    //从socket读取数据，把readlen长度的数据读取到c->querybuf+qblen的地址后
    nread = connRead(c->conn, c->querybuf+qblen, readlen);
    /**
     * 1.当read返回值大于0时，返回读到数据的实际字节数
     * 2.返回值等于0时，表示读到文件末尾。
     * 3.返回值小于0时，返回-1且设置errno
     */
    if (nread == -1) {
        //如果读取完了，
        if (connGetState(conn) == CONN_STATE_CONNECTED) {
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s",connGetLastError(c->conn));
            freeClientAsync(c);
            return;
        }
    } else if (nread == 0) {
        serverLog(LL_VERBOSE, "Client closed connection");
        freeClientAsync(c);
        return;
    } else if (c->flags & CLIENT_MASTER) {
        /* Append the query buffer to the pending (not applied) buffer
         * of the master. We'll use this buffer later in order to have a
         * copy of the string applied by the last command executed. 
         * 将查询缓冲区附加到主节点的待处理缓冲区。
         * 稍后我们将使用此缓冲区，以便获得最后的执行结果的副本。
         * 这或许用于主从复制 */
        c->pending_querybuf = sdscatlen(c->pending_querybuf,
                                        c->querybuf+qblen,nread);
    }
    /**
     * 我们在上面扩容sds的操作中，使用了一个预分配的策略，
     * 我们实际上会分配更多的空间（具体请看sdsMakeRoomFor），来避免读取数据时空间不足和污染内存，
     * 那么此刻，我们已经得知nread为读取到的实际数量，那么我们就把长度重新修剪成真实的长度，并在最后方追加\0
     * */
    sdsIncrLen(c->querybuf,nread);
    //更新最后一次交互的时间
    c->lastinteraction = server.unixtime;
    //如果是主节点，增加读取复制的偏移量
    if (c->flags & CLIENT_MASTER) c->read_reploff += nread;
    //服务器统计信息
    server.stat_net_input_bytes += nread;
    //如果读取到的总长度大于客户端查询缓冲区长度限制，那么我们会关闭客户端连接
    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {
        sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();

        bytes = sdscatrepr(bytes,c->querybuf,64);
        serverLog(LL_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
        sdsfree(ci);
        sdsfree(bytes);
        freeClientAsync(c);
        return;
    }

    /* There is more data in the client input buffer, continue parsing it
     * in case to check if there is a full command to execute. 
     客户端输入缓冲区中还有很多内容，继续解析以检查是否有完整的命令要执行。*/
     processInputBuffer(c);
}
```

在我们满心欢喜看完readQueryFromClient之后，发现它还没有对数据进行处理，仅仅只是从socket中读取数据到conn的buf数据缓存区罢了。

只不过它做的更快。得益于Redis的sds 简单动态字符串实现：

```c
//获取sds s的长度 
oldlen = sdslen(s);
//s根据BUFFER_SIZE被重新扩容，扩容后的大小，新长度为当：len(s)+BUFFER_SIZE<1024kb时，等于两倍len(s)+BUFFER_SIZE，当len(s)+BUFFER_SIZE>1024时，等于len(s)+BUFFER_SIZE+1024
//那么这里它是使用预分配的策略，会分配更多的空间来读取数据
s = sdsMakeRoomFor(s, BUFFER_SIZE);
nread = read(fd, s+oldlen, BUFFER_SIZE);
//检查nread是否小与0
//此刻，我们已经得知nread为读取到的实际数量，那么我们就把长度重新修剪成真实的长度，并在最后方追加\0
sdsIncrLen(s, nread);
```

我们把上面读取socket内容到字符串做了一个简单的概括，sds字符串的好处。它通过预分配空间的策略，传统c语言拼接字符串的缺点，需要重新开拓足够2块数据存放的空间再把两块数据复制到新空间去。这里sds字符串只使用了一次复制，最后它通过获取时机读取的字节数量，又重新把sds字符串的长度重新修剪成正确的长度，也释放掉没有使用的空间。

在读取字符串的时候了解了一下redis的sds，发现学问不少，但是我们目前最重要的任务还是继续分析Redis对客户端数据的处理。

我们来到processInputBuffer函数：





































## 下面是Redis官方内容



This README is just a fast *quick start* document. You can find more detailed documentation at [redis.io](https://redis.io).

此README文件只是快速启动文档，您可以在[redis.io](https://redis.io)找到更详细的文档。

What is Redis?
--------------
什么是redis

Redis is often referred to as a *data structures* server. What this means is that Redis provides access to mutable data structures via a set of commands, which are sent using a *server-client* model with TCP sockets and a simple protocol. So different processes can query and modify the same data structures in a shared way.

Redis通常被称为数据结构服务器。这意味着Redis提供了多种命令集去访问可修改的数据结构，这种命令使用server-client模型通过TCP sockets和简单的协议进行发送。所以不同的进程都可以通过共享的方法查询和修改同样的数据结构。

Data structures implemented into Redis have a few special properties:

Redis 中实现的数据结构有一些特殊的属性：

* Redis cares to store them on disk, even if they are always served and modified into the server memory. This means that Redis is fast, but that it is also non-volatile.
* 虽然Redis对数据都是基于内存的操作，但是Redis也会把数据存到硬盘上。这意味着Redis是快速的，但同样也是不容易丢失数据的。
* The implementation of data structures emphasizes memory efficiency, so data structures inside Redis will likely use less memory compared to the same data structure modelled using a high-level programming language.
* Redis 中实现的数据结构强调内存效率，所以在Redis内的数据结构相比起其他高级语言的相同数据结构模型会使用更少的内存
* Redis offers a number of features that are natural to find in a database, like replication, tunable levels of durability, clustering, and high availability.
* Redis提供了许多跟数据库相似的特征，例如可复制、可调整的持久性级别、集群和高可用性。

Another good example is to think of Redis as a more complex version of memcached, where the operations are not just SETs and GETs, but operations that work with complex data types like Lists, Sets, ordered data structures, and so forth.

一个很好的例子就是将Redis看成是一个更复杂的内存缓存，但是它的操作不只是set和get，还能操作一些更复杂的数据类型，像列表，集合，有序的数据结构等等。

If you want to know more, this is a list of selected starting points:

如果您想了解更多，下面是精选过的列表

* Introduction to Redis data types. http://redis.io/topics/data-types-intro
* 介绍Redis数据类型 http://redis.io/topics/data-types-intro
* Try Redis directly inside your browser. http://try.redis.io
* 通过您的浏览器直接使用Redis http://try.redis.io
* The full list of Redis commands. http://redis.io/commands
* Redis命令的列表大全 http://redis.io/commands
* There is much more inside the official Redis documentation. http://redis.io/documentation
* 还有更多内容在官方Redis文档 http://redis.io/documentation

Building Redis
--------------

构建Redis

Redis can be compiled and used on Linux, OSX, OpenBSD, NetBSD, FreeBSD.
We support big endian and little endian architectures, and both 32 bit
and 64 bit systems.

Redis可以在Linux，OSX，OpenBSD，NetBSD，FreeBSD等平台上进行编译和使用，我们支持大端和小端架构，还有32位和64位操作系统

It may compile on Solaris derived systems (for instance SmartOS) but our
support for this platform is *best effort* and Redis is not guaranteed to
work as well as in Linux, OSX, and \*BSD.

它可以在 Solaris 派生系统（例如 SmartOS）上编译，但我们对这个平台的支持是尽力而为的，并且不能保证 Redis 像在 Linux、OSX 和 BSD 中工作得一样好。

It is as simple as:

它编译非常简单：

    % make

To build with TLS support, you'll need OpenSSL development libraries (e.g.
libssl-dev on Debian/Ubuntu) and run:

为了构建的时候添加TLS支持，您可能需要OpenSSL 开发库并运行（例如 Debian/Ubuntu上的libssl-dev）

    % make BUILD_TLS=yes

To build with systemd support, you'll need systemd development libraries (such 
as libsystemd-dev on Debian/Ubuntu or systemd-devel on CentOS) and run:

为了构建的时候添加系统支持，您可能需要系统开发库并运行（例如 Debian/Ubuntu上的libsystemed 或者 CentOS上的Systemed-devel）

    % make USE_SYSTEMD=yes

To append a suffix to Redis program names, use:

为了添加后缀到Redis项目名称，可以这样使用：

    % make PROG_SUFFIX="-alt"

You can run a 32 bit Redis binary using:

您可以编译出32位Redis二进制文件：

    % make 32bit

After building Redis, it is a good idea to test it using:

构建 Redis 后，最好使用以下方法对其进行测试：

    % make test

If TLS is built, running the tests with TLS enabled (you will need `tcl-tls`
installed):

如果构建了 TLS，则在启用 TLS 的情况下运行测试（您将需要安装 `tcl-tls`）：

    % ./utils/gen-test-certs.sh
    % ./runtest --tls


Fixing build problems with dependencies or cached build options
---------
修复构建的时候出现的依赖或者缓存构建选项问题

Redis has some dependencies which are included in the `deps` directory.
`make` does not automatically rebuild dependencies even if something in
the source code of dependencies changes.

Redis有一些依赖放在了`deps`目录，`make`不会自动重新构建依赖，即使有些依赖的源文件发送了改变

When you update the source code with `git pull` or when code inside the
dependencies tree is modified in any other way, make sure to use the following
command in order to really clean everything and rebuild from scratch:

当您通过 `git pull` 更新源代码或者依赖树里面的代码已经被其他方式修改了的时候，确保您使用了下面的命令去清理所有东西并重新构建

    make distclean

This will clean: jemalloc, lua, hiredis, linenoise.

这会清理：jemalloc, lua, hiredis, linenoise。

Also if you force certain build options like 32bit target, no C compiler
optimizations (for debugging purposes), and other similar build time options,
those options are cached indefinitely until you issue a `make distclean`
command.

当然，如果您强制包含一些构建选项，例如 32位目标，
不使用C 编译器优化（为了debug）和其他相似的构建时间选项，
这些选项会被缓存起来直至您使用 `make distclean` 命令

Fixing problems building 32 bit binaries
---------

修复构建32位二进制的问题

If after building Redis with a 32 bit target you need to rebuild it
with a 64 bit target, or the other way around, you need to perform a
`make distclean` in the root directory of the Redis distribution.

如果在使用 32 位目标构建 Redis 后，您需要重新构建它
使用 64 位目标，或者相反，您需要执行
Redis 发行版根目录中的“make distclean”。

In case of build errors when trying to build a 32 bit binary of Redis, try
the following steps:

以防在尝试构建32位Redis程序的时候出现错误，尝试使用以下步骤

* Install the package libc6-dev-i386 (also try g++-multilib).
* 安装libc6-dev-i386包 （也可以尝试 g++-multilib）
* Try using the following command line instead of `make 32bit`:
  `make CFLAGS="-m32 -march=native" LDFLAGS="-m32"`
* 尝试使用如下命令代替`make 32bit`: 
  `make CFLAGS="-m32 -march=native" LDFLAGS="-m32"`

Allocator
---------
内存分配器

Selecting a non-default memory allocator when building Redis is done by setting
the `MALLOC` environment variable. Redis is compiled and linked against libc
malloc by default, with the exception of jemalloc being the default on Linux
systems. This default was picked because jemalloc has proven to have fewer
fragmentation problems than libc malloc.

在构建 Redis 时选择非默认内存分配器是通过设置 `MALLOC` 环境变量来完成的。 
默认情况下，Redis 是针对 libc malloc 编译和链接的，
但 jemalloc 是 Linux 系统上的默认设置。 
之所以选择此默认值，是因为 jemalloc 已被证明比 libc malloc具有更少的内存碎片问题。

To force compiling against libc malloc, use:

要强制针对 libc malloc 进行编译，请使用：

    % make MALLOC=libc

To compile against jemalloc on Mac OS X systems, use:

要在 Mac OS X 系统上针对 jemalloc 进行编译，请使用：

    % make MALLOC=jemalloc

Verbose build
-------------
详细构建

Redis will build with a user-friendly colorized output by default.
If you want to see a more verbose output, use the following:

默认情况下，Redis 将使用用户友好的彩色输出进行构建。
如果要查看更详细的输出，请使用以下命令：

    % make V=1

Running Redis
-------------
运行Redis

To run Redis with the default configuration, just type:
如果通过默认配置启动Redis，尝试输入

    % cd src
    % ./redis-server

If you want to provide your redis.conf, you have to run it using an additional
parameter (the path of the configuration file):

如果您想要提供您的redis.conf，您需要使用条件参数去运行它（路径是配置文件）

    % cd src
    % ./redis-server /path/to/redis.conf

It is possible to alter the Redis configuration by passing parameters directly
as options using the command line. Examples:

可以通过使用命令行直接将参数作为选项传递来更改 Redis 配置。 例子：

    % ./redis-server --port 9999 --replicaof 127.0.0.1 6379
    % ./redis-server /etc/redis/6379.conf --loglevel debug

All the options in redis.conf are also supported as options using the command
line, with exactly the same name.

所有在redis.conf里的选项都支持使用命令行通过相同的名字进行输入

Running Redis with TLS:
------------------
通过TLS运行Redis

Please consult the [TLS.md](TLS.md) file for more information on
how to use Redis with TLS.

请浏览[TLS.md](TLS.md)获取更多的信息关于如何通过TLS使用Redis

Playing with Redis
------------------
使用Redis

You can use redis-cli to play with Redis. Start a redis-server instance,
then in another terminal try the following:

您可以使用redis-cli来使用redis，开始一个redis-server实例，如何开启宁一个命令后输入下面命令：

    % cd src
    % ./redis-cli
    redis> ping
    PONG
    redis> set foo bar
    OK
    redis> get foo
    "bar"
    redis> incr mycounter
    (integer) 1
    redis> incr mycounter
    (integer) 2
    redis>

You can find the list of all the available commands at http://redis.io/commands.

您可以在 http://redis.io/commands 找到所有可用的命令列表

Installing Redis
-----------------
安装Redis

In order to install Redis binaries into /usr/local/bin, just use:

为了安装redis到/usr/local/bin，可以使用：

    % make install

You can use `make PREFIX=/some/other/directory install` if you wish to use a
different destination.

如果您想使用不同的安装路径，您可以使用`make PREFIX=/some/other/directory install`。

Make install will just install binaries in your system, but will not configure
init scripts and configuration files in the appropriate place. This is not
needed if you just want to play a bit with Redis, but if you are installing
it the proper way for a production system, we have a script that does this
for Ubuntu and Debian systems:

Make install 只会在您的系统中安装二进制文件，但不会在适当的位置配置初始化脚本和配置文件。
如果您只是想稍微玩一下 Redis，则不需要这样做，
但是如果您以正确的方式为生产系统安装它，我们有一个脚本可以为 Ubuntu 和 Debian 系统执行此操作：

    % cd utils
    % ./install_server.sh

_Note_: `install_server.sh` will not work on Mac OSX; it is built for Linux only.

_Note_: `install_server.sh` 在Mac OSX不会正常工作，它只支持Linux

The script will ask you a few questions and will setup everything you need
to run Redis properly as a background daemon that will start again on
system reboots.

该脚本将询问您几个问题，并将设置正确运行 Redis 所需的一切作为后台守护程序，该后台守护程序将在系统重新启动时重新启动。


You'll be able to stop and start Redis using the script named
`/etc/init.d/redis_<portnumber>`, for instance `/etc/init.d/redis_6379`.

您可以停止或启动Redis通过使用脚本命令
`/etc/init.d/redis_<portnumber>`, 例如 `/etc/init.d/redis_6379`.

Code contributions
-----------------
代码贡献

Note: By contributing code to the Redis project in any form, including sending
a pull request via Github, a code fragment or patch via private email or
public discussion groups, you agree to release your code under the terms
of the BSD license that you can find in the [COPYING][1] file included in the Redis
source distribution.

注意：通过以任何形式向 Redis 项目贡献代码，包括通过 Github 发送拉取请求、
通过私人电子邮件或公共讨论组发送代码片段或补丁，
您同意根据 BSD 许可条款发布您的代码，您可以 在 Redis 源代码分发包中的 [COPYING][1] 文件中找到该条款。

Please see the [CONTRIBUTING][2] file in this source distribution for more
information.

有关更多信息，请参阅此源分发中的 [CONTRIBUTING][2] 文件。

[1]: https://github.com/redis/redis/blob/unstable/COPYING
[2]: https://github.com/redis/redis/blob/unstable/CONTRIBUTING

Redis internals
===
Redis 内部结构


If you are reading this README you are likely in front of a Github page
or you just untarred the Redis distribution tar ball. In both the cases
you are basically one step away from the source code, so here we explain
the Redis source code layout, what is in each file as a general idea, the
most important functions and structures inside the Redis server and so forth.
We keep all the discussion at a high level without digging into the details
since this document would be huge otherwise and our code base changes
continuously, but a general idea should be a good starting point to
understand more. Moreover most of the code is heavily commented and easy
to follow.

如果您在阅读GitHub页面前面的README，或者您刚解压了Redis tar文件，
在这两种情况下，说明您基本上距离源代码已经只有一步之遥了
所以我们接下来需要在这里解释一下Redis的源代码结构
每个文件中的一般概念，Redis服务器内部最重要的功能和结构等等。
我们将所有讨论保持在高的层面，而不深入细节，否则这个文档会很大，而且我们的代码库会不断变化
但是一些一般概念应该是一个好的出发点来帮助我们理解更多，
此外，大部分代码都经过大量注释并且易于理解。

Source code layout
---
源代码结构

The Redis root directory just contains this README, the Makefile which
calls the real Makefile inside the `src` directory and an example
configuration for Redis and Sentinel. You can find a few shell
scripts that are used in order to execute the Redis, Redis Cluster and
Redis Sentinel unit tests, which are implemented inside the `tests`
directory.

Redis根目录下只包含README，Makefile（真正的Makefile在 `src` 目录下）和一个Redis配置哨兵的例子，
您也可以找到一些脚本用来执行Redis，Redis集群和Redis哨兵单元测试，这些都在 `tests` 文件夹中实现

Inside the root are the following important directories:

在跟目录下有以下重要的目录：

* `src`: contains the Redis implementation, written in C.
* `src`: Redis的实现，使用C语言编写
* `tests`: contains the unit tests, implemented in Tcl.
* `tests`: 单元测试，通过Tcl编写
* `deps`: contains libraries Redis uses. Everything needed to compile Redis is inside this directory; your system just needs to provide `libc`, a POSIX compatible interface and a C compiler. Notably `deps` contains a copy of 
`jemalloc`, which is the default allocator of Redis under Linux. Note that under `deps` there are also things which started with the Redis project, but for which the main repository is not `redis/redis`.
* `deps`: 包括Redis使用的一些库，所有需要编译Redis的库都在这个目录，您的系统只需要提供`libc`，一个POSIX兼容的接口和一个C编译器。 值得注意的是，`deps` 包含 `jemalloc` 的副本，它是 Linux 下 Redis 的默认内存分配器。 请注意，在 `deps` 下也有一些从 Redis 项目开始的东西，但其主存储库不是 `redis/redis`。

There are a few more directories but they are not very important for our goals
here. We'll focus mostly on `src`, where the Redis implementation is contained,
exploring what there is inside each file. The order in which files are
exposed is the logical one to follow in order to disclose different layers
of complexity incrementally.

还有一些目录，但它们对于我们在这里的目标并不是很重要。 
我们将主要关注包含 Redis 实现的 `src`，探索每个文件中的内容。
文件公开的顺序是为了逐步揭示不同层次的复杂性而遵循的逻辑顺序。

Note: lately Redis was refactored quite a bit. Function names and file
names have been changed, so you may find that this documentation reflects the
`unstable` branch more closely. For instance, in Redis 3.0 the `server.c`
and `server.h` files were named `redis.c` and `redis.h`. However the overall
structure is the same. Keep in mind that all the new developments and pull
requests should be performed against the `unstable` branch.

注意，最近的Redis进行了一点点重构，函数名称和文件名称都有被改变，所以您可能会发现这个文档更接近地反映了`unstable`分支。
例如，在Redis 3.0 中 `server.c` 和 `server.h` 文件都被命名为 `redis.c` 和 `redis.h`。
但是整体结构是一样的。 请记住，所有新开发和拉取请求都应针对`unstable`分支执行。

server.h
---

The simplest way to understand how a program works is to understand the
data structures it uses. So we'll start from the main header file of
Redis, which is `server.h`.

最简单去了解项目是如何工作的方法就是去了解数据结构是如何使用的，所以我们可以从主要的 `server.h` 头文件开始

All the server configuration and in general all the shared state is
defined in a global structure called `server`, of type `struct redisServer`.
A few important fields in this structure are:

所有服务的配置和通用的共享状态都定义在叫做`server`，类型为`struct redisServer`的数据结构中，
下面有一些这个数据结构的其他重要的字段：

* `server.db` is an array of Redis databases, where data is stored.
* `server.db` 是存储数据的 Redis 数据库数组。
* `server.commands` is the command table.
* `server.commands` 是一个命令表
* `server.clients` is a linked list of clients connected to the server.
* `server.clients` 是一个客户端连接到服务器的链表列表
* `server.master` is a special client, the master, if the instance is a replica.
* `server.master` 是一个特殊的客户端，成为master，如果该实例配置的replica

There are tons of other fields. Most fields are commented directly inside
the structure definition.

还有很多其他字段，大多数字段直接在结构定义中进行注释了。

Another important Redis data structure is the one defining a client.
In the past it was called `redisClient`, now just `client`. The structure
has many fields, here we'll just show the main ones:

另一个重要的 Redis 数据结构是定义客户端的数据结构。
过去它被称为 `redisClient`，现在只是 `client`。 该结构有很多字段，
这里我们只展示主要的：

    struct client {
        int fd;
        sds querybuf;
        int argc;
        robj **argv;
        redisDb *db;
        int flags;
        list *reply;
        char buf[PROTO_REPLY_CHUNK_BYTES];
        ... many other fields ...
    }

The client structure defines a *connected client*:
客户端数据结构定义了一个 已连接的客户

* The `fd` field is the client socket file descriptor.
* `fd` 字段是与客户端进行socket通信的文件描述符
* `argc` and `argv` are populated with the command the client is executing, so that functions implementing a given Redis command can read the arguments.
* `argc` 和 `argv` 填充了客户端正在执行的命令，因此实现给定 Redis 命令的函数可以读取参数。
* `querybuf` accumulates the requests from the client, which are parsed by the Redis server according to the Redis protocol and executed by calling the implementations of the commands the client is executing.
* `querybuf` 累积来自客户端的请求，由 Redis 服务器根据 Redis 协议进行解析，并通过调用客户端正在执行的命令的实现来执行。
* `reply` and `buf` are dynamic and static buffers that accumulate the replies the server sends to the client. These buffers are incrementally written to the socket as soon as the file descriptor is writeable.
* `reply` 和 `buf` 是动态和静态buffers缓冲区，用户积累服务器发送给客户端的回复，只要文件描述符是可写入的，这些buffers会被立即写入socket中。


As you can see in the client structure above, arguments in a command
are described as `robj` structures. The following is the full `robj`
structure, which defines a *Redis object*:

正如上面您所看到的客户端数据结构那样，命令的参数被称为`robj` 数据结构，下面是`robj`数据结构的全貌，它被定义为Redis object

    typedef struct redisObject {
        unsigned type:4;
        unsigned encoding:4;
        unsigned lru:LRU_BITS; /* lru time (relative to server.lruclock) */
        int refcount;
        void *ptr;
    } robj;

Basically this structure can represent all the basic Redis data types like
strings, lists, sets, sorted sets and so forth. The interesting thing is that
it has a `type` field, so that it is possible to know what type a given
object has, and a `refcount`, so that the same object can be referenced
in multiple places without allocating it multiple times. Finally the `ptr`
field points to the actual representation of the object, which might vary
even for the same type, depending on the `encoding` used.

基本上，该数据结构可以代表所有基础的Redis数据类型，像字符串，列表，集合，有序集合等待。
有趣的是它有一个 `type` 字段，因此可以知道给定对象的类型，
还有一个 `refcount`，因此可以在多个地方引用同一个对象而无需多次分配。
最后，`ptr`字段指向对象的实际表示，即使对于相同的类型，它也可能会有所不同，具体取决于使用的编码。

Redis objects are used extensively in the Redis internals, however in order
to avoid the overhead of indirect accesses, recently in many places
we just use plain dynamic strings not wrapped inside a Redis object.

Redis 对象在 Redis 内部被广泛使用，但是为了避免间接访问的开销，
最近在许多地方我们只使用普通的动态字符串，而不是包装在 Redis 对象中。

server.c
---

This is the entry point of the Redis server, where the `main()` function
is defined. The following are the most important steps in order to startup
the Redis server.

server.c 这是一个Redis服务的进入点，它定义了`main()`函数。下面是一些启动Redis服务的重要步骤：

* `initServerConfig()` sets up the default values of the `server` structure.
* `initServerConfig()` 为 `server` 数据结构设置默认值
* `initServer()` allocates the data structures needed to operate, setup the listening socket, and so forth.
* `initServer()` 给需要进行操作的数据结构分配内存，设置监听socket等待
* `aeMain()` starts the event loop which listens for new connections.
* `aeMain()` 开启事件循环去监听新的连接

There are two special functions called periodically by the event loop:
下面有两个特殊的函数被事件循环定期调用

1. `serverCron()` is called periodically (according to `server.hz` frequency), and performs tasks that must be performed from time to time, like checking for timed out clients.
1. `serverCron()` 被定期调用（根据`server.hz`频率）并执行需要时不时执行的任务，例如定期检查客户端是否连接超时
2. `beforeSleep()` is called every time the event loop fired, Redis served a few requests, and is returning back into the event loop.
2. `beforeSleep()` 每次触发事件循环时都会调用，Redis 服务一些请求，然后返回到事件循环中。

Inside server.c you can find code that handles other vital things of the Redis server:

在 server.c 您可以找到代码去处理Redis服务的其他重要的事情

* `call()` is used in order to call a given command in the context of a given client.
* `call()` 用于在给定客户端的上下文中调用给定的命令。
* `activeExpireCycle()` handles eviciton of keys with a time to live set via the `EXPIRE` command.
* `activeExpireCycle()` 当时间已经过了在命令中设置的过期时间之后处理把Keys移除。
* `freeMemoryIfNeeded()` is called when a new write command should be performed but Redis is out of memory according to the `maxmemory` directive.
* `freeMemoryIfNeeded()` 当有新的写入请求需要被执行但是Redis已经没有可以可以使用的内存（最大使用内存通过`maxmemory`配置）的时候，需要触发释放内存操作
* The global variable `redisCommandTable` defines all the Redis commands, specifying the name of the command, the function implementing the command, the number of arguments required, and other properties of each command.
* 全局变量`redisCommandTable`定义了所有Redis命令，指定命令的名称，实现该命令的函数，所需要的参数数量，和其他每一个命令的其他属性。

networking.c
---

This file defines all the I/O functions with clients, masters and replicas
(which in Redis are just special clients):
这个文件定义了客户端，master和副本所需的所有I/O 函数（副本在Redis也可以作为一个特殊的客户端）

* `createClient()` allocates and initializes a new client.
* `createClient()` 为新客户端分配内存
* the `addReply*()` family of functions are used by command implementations in order to append data to the client structure, that will be transmitted to the client as a reply for a given command executed.
* `addReply*()` 这一类函数用于实现命令去添加数据到客户端数据结构，这将作为对执行给定命令的回复传输给客户端。
* `writeToClient()` transmits the data pending in the output buffers to the client and is called by the *writable event handler* `sendReplyToClient()`.
* `writeToClient()` 传输在缓冲区中等待发送给客户端的数据被称为 "可写事件处理" `sendReplyToClient()`
* `readQueryFromClient()` is the *readable event handler* and accumulates data read from the client into the query buffer.
* `readQueryFromClient()` 这是一个 "可读事件处理" 将从客户端读取的数据累积到查询缓冲区中。
* `processInputBuffer()` is the entry point in order to parse the client query buffer according to the Redis protocol. Once commands are ready to be processed, it calls `processCommand()` which is defined inside `server.c` in order to actually execute the command.
* `processInputBuffer()` 这是一个根据Redis协议去解析客户端缓冲区数据的进入点，一旦命令可以准备去处理，就会调用定义在`server.c` 的 `processCommand()` 函数去真实的执行命令
* `freeClient()` deallocates, disconnects and removes a client.
* `freeClient()` 断开客户端连接，移除客户端和释放内存

aof.c and rdb.c
---

As you can guess from the names, these files implement the RDB and AOF
persistence for Redis. Redis uses a persistence model based on the `fork()`
system call in order to create a thread with the same (shared) memory
content of the main Redis thread. This secondary thread dumps the content
of the memory on disk. This is used by `rdb.c` to create the snapshots
on disk and by `aof.c` in order to perform the AOF rewrite when the
append only file gets too big.

正如您从名字就可以猜到，这些文件通过实现 RDB 和 AOF 为Redis提供持久化的能力。
Redis使用的持久化模型是基于`fork()`系统调用去创建一个和Redis主线程有相同共享内存的线程。
第二个线程把内存里的内容保存到硬盘，它使用 `rdb.c` 去创建一个磁盘快照文件，
并通过`aof.c`为了在文件追加变得太大之后执行AOF重写


The implementation inside `aof.c` has additional functions in order to
implement an API that allows commands to append new commands into the AOF
file as clients execute them.

`aof.c`中实现了一些额外的函数可供来允许通过调用API允许命令在客户端执行它们时将新命令附加到 AOF 文件中。


The `call()` function defined inside `server.c` is responsible for calling
the functions that in turn will write the commands into the AOF.

在 `server.c` 中定义的 `call()` 函数负责调用这些函数，这些函数又会将命令写入 AOF。

db.c
---

Certain Redis commands operate on specific data types; others are general.
Examples of generic commands are `DEL` and `EXPIRE`. They operate on keys
and not on their values specifically. All those generic commands are
defined inside `db.c`.

特定的Redis命令作用于特定的数据类型；
其他通用的命令例如 `DEL` 和 `EXPIRE`，这些操作是作用于keys上面的，而不是特定的值。
所有这些通用的命令都定义在`db.c`

Moreover `db.c` implements an API in order to perform certain operations
on the Redis dataset without directly accessing the internal data structures.

此外，`db.c` 实现了一个 API，以便在不直接访问内部数据结构的情况下对 Redis 数据集执行某些操作。


The most important functions inside `db.c` which are used in many command
implementations are the following:

在许多命令里都涉及 `db.c` 里的重要的函数实现如下：

* `lookupKeyRead()` and `lookupKeyWrite()` are used in order to get a pointer to the value associated to a given key, or `NULL` if the key does not exist.
* `lookupKeyRead()` 和 `lookupKeyWrite()` 用来得到一个指向给定key的value值的指针，如果key不存在，会返回NULL
* `dbAdd()` and its higher level counterpart `setKey()` create a new key in a Redis database.
* `dbAdd()` 及其更高级别的对应项 `setKey()` 在 Redis 数据库中创建一个新键。
* `dbDelete()` removes a key and its associated value.
* `dbDelete()` 移除一个key和它对应的value
* `emptyDb()` removes an entire single database or all the databases defined.
* `emptyDb()` 清空整个单独数据库或所有定义的数据库

The rest of the file implements the generic commands exposed to the client.

剩下的文件实现了一些通用的命令暴露给客户端使用

object.c
---

The `robj` structure defining Redis objects was already described. Inside
`object.c` there are all the functions that operate with Redis objects at
a basic level, like functions to allocate new objects, handle the reference
counting and so forth. Notable functions inside this file:

`robj` 数据结构定义了一些已经被描述的Redis objects. 在 `object.c` 里有所有对Redis objects做基础操作的函数，
像分配一个新object，处理引用计数等值的关注的函数在里面。

* `incrRefCount()` and `decrRefCount()` are used in order to increment or decrement an object reference count. When it drops to 0 the object is finally freed.
* `incrRefCount()` 和 `decrRefCount()` 用来增加或减少对象的引用，当对象的引用减少到0的时候，才会被真正清理。
* `createObject()` allocates a new object. There are also specialized functions to allocate string objects having a specific content, like `createStringObjectFromLongLong()` and similar functions.
* `createObject()` 分配一个新对象。也有一些特殊的函数去分配一个特定内容的string对象，像`createStringObjectFromLongLong()`和一些相似的函数。

This file also implements the `OBJECT` command.

文件也实现了`OBJECT`命令

replication.c
---

This is one of the most complex files inside Redis, it is recommended to
approach it only after getting a bit familiar with the rest of the code base.
In this file there is the implementation of both the master and replica role
of Redis.

这是Redis里最为复杂的一个文件，建议对源代码有一点的深入之后再去了解它。这里包含了Redis master和replica 角色之间的一些实现。

One of the most important functions inside this file is `replicationFeedSlaves()` that writes commands to the clients representing replica instances connected
to our master, so that the replicas can get the writes performed by the clients:
this way their data set will remain synchronized with the one in the master.

在这个文件里最重要的一个方法 `replicationFeedSlaves()` 是用来把Master节点得到的命令写入slave客户端，以便slave客户端可以获取客户端执行的写入，这样，他们的数据集将与Master服务器中的数据集保持同步。

This file also implements both the `SYNC` and `PSYNC` commands that are
used in order to perform the first synchronization between masters and
replicas, or to continue the replication after a disconnection.

这个文件也实现了`SYNC` 和 `PSYNC` 命令用来第一次执行Master和slave节点同步，或者在断开连接之后继续复制

Other C files
---
其他 C 文件

* `t_hash.c`, `t_list.c`, `t_set.c`, `t_string.c`, `t_zset.c` and `t_stream.c` contains the implementation of the Redis data types. They implement both an API to access a given data type, and the client command implementations for these data types.
* `t_hash.c`, `t_list.c`, `t_set.c`, `t_string.c`, `t_zset.c` 和 `t_stream.c` 包含了Redis数据类型的实现，他们既实现了访问数据类型的API 也实现了这些数据结构的客户端命令
* `ae.c` implements the Redis event loop, it's a self contained library which is simple to read and understand.
* `ae.c` 实现了Redis事件循环，这是一个非常简单和易于阅读了理解的独立库
* `sds.c` is the Redis string library, check http://github.com/antirez/sds for more information.
* `sds.c` 是一个Redis string 库，阅读 http://github.com/antirez/sds 了解更多信息
* `anet.c` is a library to use POSIX networking in a simpler way compared to the raw interface exposed by the kernel.
* `anet.c` 相比起使用内核暴露的接口，这是一个更简便使用 POSIX 网络的一个库
* `dict.c` is an implementation of a non-blocking hash table which rehashes incrementally.
* `dict.c` 是一个扩容重哈希时非阻塞的hashtable的实现
* `scripting.c` implements Lua scripting. It is completely self-contained and isolated from the rest of the Redis implementation and is simple enough to understand if you are familiar with the Lua API.
* `scripting.c` 实现了lua脚本。它是完全独立的和Redis实现的其他部分隔离出来，如果您对Lua API熟悉，那么将会非常容易理解。
* `cluster.c` implements the Redis Cluster. Probably a good read only after being very familiar with the rest of the Redis code base. If you want to read `cluster.c` make sure to read the [Redis Cluster specification][3].
* `cluster.c` 实现了Redis集群，可能要在对源码有足够的了解的时候才能很好的阅读，如果您想要阅读`cluster.c` 确保您之前阅读了[Redis Cluster specification][3]

[3]: http://redis.io/topics/cluster-spec

Anatomy of a Redis command
---
解剖Redis命令

All the Redis commands are defined in the following way:
所有Redis命令都像下面的那样定义：

    void foobarCommand(client *c) {
        printf("%s",c->argv[1]->ptr); /* Do something with the argument. */
        addReply(c,shared.ok); /* Reply something to the client. */
    }

The command is then referenced inside `server.c` in the command table:
这个命令在`server.c`命令表里被记录

    {"foobar",foobarCommand,2,"rtF",0,NULL,0,0,0,0,0},

In the above example `2` is the number of arguments the command takes,
while `"rtF"` are the command flags, as documented in the command table
top comment inside `server.c`.

在上面的例子中，`2`是命令获取的参数数量，而 `rtF` 是命令标志，如 `server.c` 中的命令表顶部注释中所述。

After the command operates in some way, it returns a reply to the client,
usually using `addReply()` or a similar function defined inside `networking.c`.

在命令以某种方法被执行之后，它会使用 `addReply()` 或者 相似的定义在 `networking.c` 里的函数把回复返回给客户端

There are tons of command implementations inside the Redis source code
that can serve as examples of actual commands implementations. Writing
a few toy commands can be a good exercise to get familiar with the code base.

还有许多的命令实现在Redis源代码里面，源代码里提供了一些真实命令实现的例子。写一些命令将会是对熟悉源码最好的练习。

There are also many other files not described here, but it is useless to
cover everything. We just want to help you with the first steps.
Eventually you'll find your way inside the Redis code base :-)

还有许多我们没有在这里提及到的文件，这里不足以覆盖所有的事情，我们只是想帮您开始第一步。
最终，您会找到您的方法进入到Redis源代码里。

Enjoy!
开始吧！