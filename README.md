This README is just a fast *quick start* document. You can find more detailed documentation at [redis.io](https://redis.io).

此README文件只是快速启动文档，你可以在[redis.io](https://redis.io)找到更详细的文档。

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

为了构建的时候添加TLS支持，你可能需要OpenSSL 开发库并运行（例如 Debian/Ubuntu上的libssl-dev）

    % make BUILD_TLS=yes

To build with systemd support, you'll need systemd development libraries (such 
as libsystemd-dev on Debian/Ubuntu or systemd-devel on CentOS) and run:

为了构建的时候添加系统支持，你可能需要系统开发库并运行（例如 Debian/Ubuntu上的libsystemed 或者 CentOS上的Systemed-devel）

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

当你通过 `git pull` 更新源代码或者依赖树里面的代码已经被其他方式修改了的时候，确保你使用了下面的命令去清理所有东西并重新构建

    make distclean

This will clean: jemalloc, lua, hiredis, linenoise.

这会清理：jemalloc, lua, hiredis, linenoise。

Also if you force certain build options like 32bit target, no C compiler
optimizations (for debugging purposes), and other similar build time options,
those options are cached indefinitely until you issue a `make distclean`
command.

当然，如果你强制包含一些构建选项，例如 32位目标，
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

如果你想要提供您的redis.conf，你需要使用条件参数去运行它（路径是配置文件）

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

你可以使用redis-cli来使用redis，开始一个redis-server实例，如何开启宁一个命令后输入下面命令：

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

你可以在 http://redis.io/commands 找到所有可用的命令列表

Installing Redis
-----------------
安装Redis

In order to install Redis binaries into /usr/local/bin, just use:

为了安装redis到/usr/local/bin，可以使用：

    % make install

You can use `make PREFIX=/some/other/directory install` if you wish to use a
different destination.

如果你想使用不同的安装路径，你可以使用`make PREFIX=/some/other/directory install`。

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

The script will ask you a few questions and will setup everything you need
to run Redis properly as a background daemon that will start again on
system reboots.

You'll be able to stop and start Redis using the script named
`/etc/init.d/redis_<portnumber>`, for instance `/etc/init.d/redis_6379`.

Code contributions
-----------------

Note: By contributing code to the Redis project in any form, including sending
a pull request via Github, a code fragment or patch via private email or
public discussion groups, you agree to release your code under the terms
of the BSD license that you can find in the [COPYING][1] file included in the Redis
source distribution.

Please see the [CONTRIBUTING][2] file in this source distribution for more
information.

[1]: https://github.com/redis/redis/blob/unstable/COPYING
[2]: https://github.com/redis/redis/blob/unstable/CONTRIBUTING

Redis internals
===

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

Source code layout
---

The Redis root directory just contains this README, the Makefile which
calls the real Makefile inside the `src` directory and an example
configuration for Redis and Sentinel. You can find a few shell
scripts that are used in order to execute the Redis, Redis Cluster and
Redis Sentinel unit tests, which are implemented inside the `tests`
directory.

Inside the root are the following important directories:

* `src`: contains the Redis implementation, written in C.
* `tests`: contains the unit tests, implemented in Tcl.
* `deps`: contains libraries Redis uses. Everything needed to compile Redis is inside this directory; your system just needs to provide `libc`, a POSIX compatible interface and a C compiler. Notably `deps` contains a copy of `jemalloc`, which is the default allocator of Redis under Linux. Note that under `deps` there are also things which started with the Redis project, but for which the main repository is not `redis/redis`.

There are a few more directories but they are not very important for our goals
here. We'll focus mostly on `src`, where the Redis implementation is contained,
exploring what there is inside each file. The order in which files are
exposed is the logical one to follow in order to disclose different layers
of complexity incrementally.

Note: lately Redis was refactored quite a bit. Function names and file
names have been changed, so you may find that this documentation reflects the
`unstable` branch more closely. For instance, in Redis 3.0 the `server.c`
and `server.h` files were named `redis.c` and `redis.h`. However the overall
structure is the same. Keep in mind that all the new developments and pull
requests should be performed against the `unstable` branch.

server.h
---

The simplest way to understand how a program works is to understand the
data structures it uses. So we'll start from the main header file of
Redis, which is `server.h`.

All the server configuration and in general all the shared state is
defined in a global structure called `server`, of type `struct redisServer`.
A few important fields in this structure are:

* `server.db` is an array of Redis databases, where data is stored.
* `server.commands` is the command table.
* `server.clients` is a linked list of clients connected to the server.
* `server.master` is a special client, the master, if the instance is a replica.

There are tons of other fields. Most fields are commented directly inside
the structure definition.

Another important Redis data structure is the one defining a client.
In the past it was called `redisClient`, now just `client`. The structure
has many fields, here we'll just show the main ones:

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

* The `fd` field is the client socket file descriptor.
* `argc` and `argv` are populated with the command the client is executing, so that functions implementing a given Redis command can read the arguments.
* `querybuf` accumulates the requests from the client, which are parsed by the Redis server according to the Redis protocol and executed by calling the implementations of the commands the client is executing.
* `reply` and `buf` are dynamic and static buffers that accumulate the replies the server sends to the client. These buffers are incrementally written to the socket as soon as the file descriptor is writeable.

As you can see in the client structure above, arguments in a command
are described as `robj` structures. The following is the full `robj`
structure, which defines a *Redis object*:

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

Redis objects are used extensively in the Redis internals, however in order
to avoid the overhead of indirect accesses, recently in many places
we just use plain dynamic strings not wrapped inside a Redis object.

server.c
---

This is the entry point of the Redis server, where the `main()` function
is defined. The following are the most important steps in order to startup
the Redis server.

* `initServerConfig()` sets up the default values of the `server` structure.
* `initServer()` allocates the data structures needed to operate, setup the listening socket, and so forth.
* `aeMain()` starts the event loop which listens for new connections.

There are two special functions called periodically by the event loop:

1. `serverCron()` is called periodically (according to `server.hz` frequency), and performs tasks that must be performed from time to time, like checking for timed out clients.
2. `beforeSleep()` is called every time the event loop fired, Redis served a few requests, and is returning back into the event loop.

Inside server.c you can find code that handles other vital things of the Redis server:

* `call()` is used in order to call a given command in the context of a given client.
* `activeExpireCycle()` handles eviciton of keys with a time to live set via the `EXPIRE` command.
* `freeMemoryIfNeeded()` is called when a new write command should be performed but Redis is out of memory according to the `maxmemory` directive.
* The global variable `redisCommandTable` defines all the Redis commands, specifying the name of the command, the function implementing the command, the number of arguments required, and other properties of each command.

networking.c
---

This file defines all the I/O functions with clients, masters and replicas
(which in Redis are just special clients):

* `createClient()` allocates and initializes a new client.
* the `addReply*()` family of functions are used by command implementations in order to append data to the client structure, that will be transmitted to the client as a reply for a given command executed.
* `writeToClient()` transmits the data pending in the output buffers to the client and is called by the *writable event handler* `sendReplyToClient()`.
* `readQueryFromClient()` is the *readable event handler* and accumulates data read from the client into the query buffer.
* `processInputBuffer()` is the entry point in order to parse the client query buffer according to the Redis protocol. Once commands are ready to be processed, it calls `processCommand()` which is defined inside `server.c` in order to actually execute the command.
* `freeClient()` deallocates, disconnects and removes a client.

aof.c and rdb.c
---

As you can guess from the names, these files implement the RDB and AOF
persistence for Redis. Redis uses a persistence model based on the `fork()`
system call in order to create a thread with the same (shared) memory
content of the main Redis thread. This secondary thread dumps the content
of the memory on disk. This is used by `rdb.c` to create the snapshots
on disk and by `aof.c` in order to perform the AOF rewrite when the
append only file gets too big.

The implementation inside `aof.c` has additional functions in order to
implement an API that allows commands to append new commands into the AOF
file as clients execute them.

The `call()` function defined inside `server.c` is responsible for calling
the functions that in turn will write the commands into the AOF.

db.c
---

Certain Redis commands operate on specific data types; others are general.
Examples of generic commands are `DEL` and `EXPIRE`. They operate on keys
and not on their values specifically. All those generic commands are
defined inside `db.c`.

Moreover `db.c` implements an API in order to perform certain operations
on the Redis dataset without directly accessing the internal data structures.

The most important functions inside `db.c` which are used in many command
implementations are the following:

* `lookupKeyRead()` and `lookupKeyWrite()` are used in order to get a pointer to the value associated to a given key, or `NULL` if the key does not exist.
* `dbAdd()` and its higher level counterpart `setKey()` create a new key in a Redis database.
* `dbDelete()` removes a key and its associated value.
* `emptyDb()` removes an entire single database or all the databases defined.

The rest of the file implements the generic commands exposed to the client.

object.c
---

The `robj` structure defining Redis objects was already described. Inside
`object.c` there are all the functions that operate with Redis objects at
a basic level, like functions to allocate new objects, handle the reference
counting and so forth. Notable functions inside this file:

* `incrRefCount()` and `decrRefCount()` are used in order to increment or decrement an object reference count. When it drops to 0 the object is finally freed.
* `createObject()` allocates a new object. There are also specialized functions to allocate string objects having a specific content, like `createStringObjectFromLongLong()` and similar functions.

This file also implements the `OBJECT` command.

replication.c
---

This is one of the most complex files inside Redis, it is recommended to
approach it only after getting a bit familiar with the rest of the code base.
In this file there is the implementation of both the master and replica role
of Redis.

One of the most important functions inside this file is `replicationFeedSlaves()` that writes commands to the clients representing replica instances connected
to our master, so that the replicas can get the writes performed by the clients:
this way their data set will remain synchronized with the one in the master.

This file also implements both the `SYNC` and `PSYNC` commands that are
used in order to perform the first synchronization between masters and
replicas, or to continue the replication after a disconnection.

Other C files
---

* `t_hash.c`, `t_list.c`, `t_set.c`, `t_string.c`, `t_zset.c` and `t_stream.c` contains the implementation of the Redis data types. They implement both an API to access a given data type, and the client command implementations for these data types.
* `ae.c` implements the Redis event loop, it's a self contained library which is simple to read and understand.
* `sds.c` is the Redis string library, check http://github.com/antirez/sds for more information.
* `anet.c` is a library to use POSIX networking in a simpler way compared to the raw interface exposed by the kernel.
* `dict.c` is an implementation of a non-blocking hash table which rehashes incrementally.
* `scripting.c` implements Lua scripting. It is completely self-contained and isolated from the rest of the Redis implementation and is simple enough to understand if you are familiar with the Lua API.
* `cluster.c` implements the Redis Cluster. Probably a good read only after being very familiar with the rest of the Redis code base. If you want to read `cluster.c` make sure to read the [Redis Cluster specification][3].

[3]: http://redis.io/topics/cluster-spec

Anatomy of a Redis command
---

All the Redis commands are defined in the following way:

    void foobarCommand(client *c) {
        printf("%s",c->argv[1]->ptr); /* Do something with the argument. */
        addReply(c,shared.ok); /* Reply something to the client. */
    }

The command is then referenced inside `server.c` in the command table:

    {"foobar",foobarCommand,2,"rtF",0,NULL,0,0,0,0,0},

In the above example `2` is the number of arguments the command takes,
while `"rtF"` are the command flags, as documented in the command table
top comment inside `server.c`.

After the command operates in some way, it returns a reply to the client,
usually using `addReply()` or a similar function defined inside `networking.c`.

There are tons of command implementations inside the Redis source code
that can serve as examples of actual commands implementations. Writing
a few toy commands can be a good exercise to get familiar with the code base.

There are also many other files not described here, but it is useless to
cover everything. We just want to help you with the first steps.
Eventually you'll find your way inside the Redis code base :-)

Enjoy!
