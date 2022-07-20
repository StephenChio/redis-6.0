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