/*
 * Copyright (c) 2009-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 如果满足下面的要求，可以以源代码和二进制形式重新分发和使用，无论是否做出修改
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *     重新分发源代码必须保留上述版权声明、此条件列表和以下免责声明。
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *     二进制形式的再分发必须在随分发提供的文档和/或其他材料中复制上述版权声明、此条件列表和以下免责声明。
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *     未经事先明确的书面许可，不得使用 Redis 的名称或其贡献者的名称来认可或推广源自该软件的产品。
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 本软件由版权所有者和贡献者提供，并且不提供任何明示或默示的保证，
 * 包括但不限于对适销性和特定用途适用性的默示保证。
 * 在任何情况下，版权所有者或贡献者均不对任何直接、间接、偶然、
 * 特殊发生的惩戒性或后果性损害负责（包括但不限于采购替代商品或服务；使用、数据或利润损失； 或业务中断），
 * 无论是由何种责任理论引起的，无论是在合同、严格责任或侵权行为中以任何方式使用本软件引起的，即使已被告知存在此类损害的可能性。
 * （包括疏忽或其他方式）
 */

#include "server.h"
#include "cluster.h"
#include "slowlog.h"
#include "bio.h"
#include "latency.h"
#include "atomicvar.h"
#include "mt19937-64.h"

#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <locale.h>
#include <sys/socket.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

/* Our shared "common" objects 我们的共享通用对象 */

struct sharedObjectsStruct shared;

/* Global vars that are actually used as constants. The following double
 * values are used for double on-disk serialization, and are initialized
 * at runtime to avoid strange compiler optimizations.
 * 实际用作常量的全局变量。
 * 以下double值用于双磁盘序列化，并在运行时初始化以避免奇怪的编译器优化。
 * */

double R_Zero, R_PosInf, R_NegInf, R_Nan;

/*================================= Globals ================================= */

/* Global vars 全局变量*/
struct redisServer server; /* Server global state 服务器全局状态*/

/* Our command table. 我们的命令表
 *
 * Every entry is composed of the following fields: 每一个条目都由下述字段组成
 *
 * name:        A string representing the command name.
 *
 * function:    Pointer to the C function implementing the command.
 *
 * arity:       Number of arguments, it is possible to use -N to say >= N
 *
 * sflags:      Command flags as string. See below for a table of flags.
 *
 * flags:       Flags as bitmask. Computed by Redis using the 'sflags' field.
 *
 * get_keys_proc: An optional function to get key arguments from a command.
 *                This is only used when the following three fields are not
 *                enough to specify what arguments are keys.
 *
 * first_key_index: First argument that is a key
 *
 * last_key_index: Last argument that is a key
 *
 * key_step:    Step to get all the keys from first to last argument.
 *              For instance in MSET the step is two since arguments
 *              are key,val,key,val,...
 *
 * microseconds: Microseconds of total execution time for this command.
 *
 * calls:       Total number of calls of this command.
 *
 * id:          Command bit identifier for ACLs or other goals.
 *
 * The flags, microseconds and calls fields are computed by Redis and should
 * always be set to zero.
 *
 * Command flags are expressed using space separated strings, that are turned
 * into actual flags by the populateCommandTable() function.
 *
 * This is the meaning of the flags:
 *
 * write:       Write command (may modify the key space).
 *
 * read-only:   All the non special commands just reading from keys without
 *              changing the content, or returning other information like
 *              the TIME command. Special commands such administrative commands
 *              or transaction related commands (multi, exec, discard, ...)
 *              are not flagged as read-only commands, since they affect the
 *              server or the connection in other ways.
 *
 * use-memory:  May increase memory usage once called. Don't allow if out
 *              of memory.
 *
 * admin:       Administrative command, like SAVE or SHUTDOWN.
 *
 * pub-sub:     Pub/Sub related command.
 *
 * no-script:   Command not allowed in scripts.
 *
 * random:      Random command. Command is not deterministic, that is, the same
 *              command with the same arguments, with the same key space, may
 *              have different results. For instance SPOP and RANDOMKEY are
 *              two random commands.
 *
 * to-sort:     Sort command output array if called from script, so that the
 *              output is deterministic. When this flag is used (not always
 *              possible), then the "random" flag is not needed.
 *
 * ok-loading:  Allow the command while loading the database.
 *
 * ok-stale:    Allow the command while a slave has stale data but is not
 *              allowed to serve this data. Normally no command is accepted
 *              in this condition but just a few.
 *
 * no-monitor:  Do not automatically propagate the command on MONITOR.
 *
 * no-slowlog:  Do not automatically propagate the command to the slowlog.
 *
 * cluster-asking: Perform an implicit ASKING for this command, so the
 *              command will be accepted in cluster mode if the slot is marked
 *              as 'importing'.
 *
 * fast:        Fast command: O(1) or O(log(N)) command that should never
 *              delay its execution as long as the kernel scheduler is giving
 *              us time. Note that commands that may trigger a DEL as a side
 *              effect (like SET) are not fast commands.
 *
 * The following additional flags are only used in order to put commands
 * in a specific ACL category. Commands can have multiple ACL categories.
 *
 * @keyspace, @read, @write, @set, @sortedset, @list, @hash, @string, @bitmap,
 * @hyperloglog, @stream, @admin, @fast, @slow, @pubsub, @blocking, @dangerous,
 * @connection, @transaction, @scripting, @geo.
 *
 * Note that:
 *
 * 1) The read-only flag implies the @read ACL category.
 * 2) The write flag implies the @write ACL category.
 * 3) The fast flag implies the @fast ACL category.
 * 4) The admin flag implies the @admin and @dangerous ACL category.
 * 5) The pub-sub flag implies the @pubsub ACL category.
 * 6) The lack of fast flag implies the @slow ACL category.
 * 7) The non obvious "keyspace" category includes the commands
 *    that interact with keys without having anything to do with
 *    specific data structures, such as: DEL, RENAME, MOVE, SELECT,
 *    TYPE, EXPIRE*, PEXPIRE*, TTL, PTTL, ...
 */

struct redisCommand redisCommandTable[] = {
    {"module", moduleCommand, -2,
     "admin no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"get", getCommand, 2,
     "read-only fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    /* Note that we can't flag set as fast, since it may perform an
     * implicit DEL of a large key. */
    {"set", setCommand, -3,
     "write use-memory @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"setnx", setnxCommand, 3,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"setex", setexCommand, 4,
     "write use-memory @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"psetex", psetexCommand, 4,
     "write use-memory @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"append", appendCommand, 3,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"strlen", strlenCommand, 2,
     "read-only fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"del", delCommand, -2,
     "write @keyspace",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"unlink", unlinkCommand, -2,
     "write fast @keyspace",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"exists", existsCommand, -2,
     "read-only fast @keyspace",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"setbit", setbitCommand, 4,
     "write use-memory @bitmap",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"getbit", getbitCommand, 3,
     "read-only fast @bitmap",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"bitfield", bitfieldCommand, -2,
     "write use-memory @bitmap",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"bitfield_ro", bitfieldroCommand, -2,
     "read-only fast @bitmap",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"setrange", setrangeCommand, 4,
     "write use-memory @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"getrange", getrangeCommand, 4,
     "read-only @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"substr", getrangeCommand, 4,
     "read-only @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"incr", incrCommand, 2,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"decr", decrCommand, 2,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"mget", mgetCommand, -2,
     "read-only fast @string",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"rpush", rpushCommand, -3,
     "write use-memory fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lpush", lpushCommand, -3,
     "write use-memory fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"rpushx", rpushxCommand, -3,
     "write use-memory fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lpushx", lpushxCommand, -3,
     "write use-memory fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"linsert", linsertCommand, 5,
     "write use-memory @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"rpop", rpopCommand, 2,
     "write fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lpop", lpopCommand, 2,
     "write fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"brpop", brpopCommand, -3,
     "write no-script @list @blocking",
     0, NULL, 1, -2, 1, 0, 0, 0},

    {"brpoplpush", brpoplpushCommand, 4,
     "write use-memory no-script @list @blocking",
     0, NULL, 1, 2, 1, 0, 0, 0},

    {"blpop", blpopCommand, -3,
     "write no-script @list @blocking",
     0, NULL, 1, -2, 1, 0, 0, 0},

    {"llen", llenCommand, 2,
     "read-only fast @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lindex", lindexCommand, 3,
     "read-only @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lset", lsetCommand, 4,
     "write use-memory @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lrange", lrangeCommand, 4,
     "read-only @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"ltrim", ltrimCommand, 4,
     "write @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lpos", lposCommand, -3,
     "read-only @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"lrem", lremCommand, 4,
     "write @list",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"rpoplpush", rpoplpushCommand, 3,
     "write use-memory @list",
     0, NULL, 1, 2, 1, 0, 0, 0},

    {"sadd", saddCommand, -3,
     "write use-memory fast @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"srem", sremCommand, -3,
     "write fast @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"smove", smoveCommand, 4,
     "write fast @set",
     0, NULL, 1, 2, 1, 0, 0, 0},

    {"sismember", sismemberCommand, 3,
     "read-only fast @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"scard", scardCommand, 2,
     "read-only fast @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"spop", spopCommand, -2,
     "write random fast @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"srandmember", srandmemberCommand, -2,
     "read-only random @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"sinter", sinterCommand, -2,
     "read-only to-sort @set",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"sinterstore", sinterstoreCommand, -3,
     "write use-memory @set",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"sunion", sunionCommand, -2,
     "read-only to-sort @set",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"sunionstore", sunionstoreCommand, -3,
     "write use-memory @set",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"sdiff", sdiffCommand, -2,
     "read-only to-sort @set",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"sdiffstore", sdiffstoreCommand, -3,
     "write use-memory @set",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"smembers", sinterCommand, 2,
     "read-only to-sort @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"sscan", sscanCommand, -3,
     "read-only random @set",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zadd", zaddCommand, -4,
     "write use-memory fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zincrby", zincrbyCommand, 4,
     "write use-memory fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrem", zremCommand, -3,
     "write fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zremrangebyscore", zremrangebyscoreCommand, 4,
     "write @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zremrangebyrank", zremrangebyrankCommand, 4,
     "write @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zremrangebylex", zremrangebylexCommand, 4,
     "write @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zunionstore", zunionstoreCommand, -4,
     "write use-memory @sortedset",
     0, zunionInterGetKeys, 1, 1, 1, 0, 0, 0},

    {"zinterstore", zinterstoreCommand, -4,
     "write use-memory @sortedset",
     0, zunionInterGetKeys, 1, 1, 1, 0, 0, 0},

    {"zrange", zrangeCommand, -4,
     "read-only @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrangebyscore", zrangebyscoreCommand, -4,
     "read-only @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrevrangebyscore", zrevrangebyscoreCommand, -4,
     "read-only @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrangebylex", zrangebylexCommand, -4,
     "read-only @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrevrangebylex", zrevrangebylexCommand, -4,
     "read-only @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zcount", zcountCommand, 4,
     "read-only fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zlexcount", zlexcountCommand, 4,
     "read-only fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrevrange", zrevrangeCommand, -4,
     "read-only @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zcard", zcardCommand, 2,
     "read-only fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zscore", zscoreCommand, 3,
     "read-only fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrank", zrankCommand, 3,
     "read-only fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zrevrank", zrevrankCommand, 3,
     "read-only fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zscan", zscanCommand, -3,
     "read-only random @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zpopmin", zpopminCommand, -2,
     "write fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"zpopmax", zpopmaxCommand, -2,
     "write fast @sortedset",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"bzpopmin", bzpopminCommand, -3,
     "write no-script fast @sortedset @blocking",
     0, NULL, 1, -2, 1, 0, 0, 0},

    {"bzpopmax", bzpopmaxCommand, -3,
     "write no-script fast @sortedset @blocking",
     0, NULL, 1, -2, 1, 0, 0, 0},

    {"hset", hsetCommand, -4,
     "write use-memory fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hsetnx", hsetnxCommand, 4,
     "write use-memory fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hget", hgetCommand, 3,
     "read-only fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hmset", hsetCommand, -4,
     "write use-memory fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hmget", hmgetCommand, -3,
     "read-only fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hincrby", hincrbyCommand, 4,
     "write use-memory fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hincrbyfloat", hincrbyfloatCommand, 4,
     "write use-memory fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hdel", hdelCommand, -3,
     "write fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hlen", hlenCommand, 2,
     "read-only fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hstrlen", hstrlenCommand, 3,
     "read-only fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hkeys", hkeysCommand, 2,
     "read-only to-sort @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hvals", hvalsCommand, 2,
     "read-only to-sort @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hgetall", hgetallCommand, 2,
     "read-only random @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hexists", hexistsCommand, 3,
     "read-only fast @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"hscan", hscanCommand, -3,
     "read-only random @hash",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"incrby", incrbyCommand, 3,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"decrby", decrbyCommand, 3,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"incrbyfloat", incrbyfloatCommand, 3,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"getset", getsetCommand, 3,
     "write use-memory fast @string",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"mset", msetCommand, -3,
     "write use-memory @string",
     0, NULL, 1, -1, 2, 0, 0, 0},

    {"msetnx", msetnxCommand, -3,
     "write use-memory @string",
     0, NULL, 1, -1, 2, 0, 0, 0},

    {"randomkey", randomkeyCommand, 1,
     "read-only random @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"select", selectCommand, 2,
     "ok-loading fast ok-stale @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"swapdb", swapdbCommand, 3,
     "write fast @keyspace @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"move", moveCommand, 3,
     "write fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    /* Like for SET, we can't mark rename as a fast command because
     * overwriting the target key may result in an implicit slow DEL. */
    {"rename", renameCommand, 3,
     "write @keyspace",
     0, NULL, 1, 2, 1, 0, 0, 0},

    {"renamenx", renamenxCommand, 3,
     "write fast @keyspace",
     0, NULL, 1, 2, 1, 0, 0, 0},

    {"expire", expireCommand, 3,
     "write fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"expireat", expireatCommand, 3,
     "write fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"pexpire", pexpireCommand, 3,
     "write fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"pexpireat", pexpireatCommand, 3,
     "write fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"keys", keysCommand, 2,
     "read-only to-sort @keyspace @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"scan", scanCommand, -2,
     "read-only random @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"dbsize", dbsizeCommand, 1,
     "read-only fast @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"auth", authCommand, -2,
     "no-auth no-script ok-loading ok-stale fast no-monitor no-slowlog @connection",
     0, NULL, 0, 0, 0, 0, 0, 0},

    /* We don't allow PING during loading since in Redis PING is used as
     * failure detection, and a loading server is considered to be
     * not available. */
    {"ping", pingCommand, -1,
     "ok-stale fast @connection",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"echo", echoCommand, 2,
     "read-only fast @connection",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"save", saveCommand, 1,
     "admin no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"bgsave", bgsaveCommand, -1,
     "admin no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"bgrewriteaof", bgrewriteaofCommand, 1,
     "admin no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"shutdown", shutdownCommand, -1,
     "admin no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"lastsave", lastsaveCommand, 1,
     "read-only random fast ok-loading ok-stale @admin @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"type", typeCommand, 2,
     "read-only fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"multi", multiCommand, 1,
     "no-script fast ok-loading ok-stale @transaction",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"exec", execCommand, 1,
     "no-script no-monitor no-slowlog ok-loading ok-stale @transaction",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"discard", discardCommand, 1,
     "no-script fast ok-loading ok-stale @transaction",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"sync", syncCommand, 1,
     "admin no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"psync", syncCommand, 3,
     "admin no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"replconf", replconfCommand, -1,
     "admin no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"flushdb", flushdbCommand, -1,
     "write @keyspace @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"flushall", flushallCommand, -1,
     "write @keyspace @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"sort", sortCommand, -2,
     "write use-memory @list @set @sortedset @dangerous",
     0, sortGetKeys, 1, 1, 1, 0, 0, 0},

    {"info", infoCommand, -1,
     "ok-loading ok-stale random @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"monitor", monitorCommand, 1,
     "admin no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"ttl", ttlCommand, 2,
     "read-only fast random @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"touch", touchCommand, -2,
     "read-only fast @keyspace",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"pttl", pttlCommand, 2,
     "read-only fast random @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"persist", persistCommand, 2,
     "write fast @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"slaveof", replicaofCommand, 3,
     "admin no-script ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"replicaof", replicaofCommand, 3,
     "admin no-script ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"role", roleCommand, 1,
     "ok-loading ok-stale no-script fast read-only @dangerous",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"debug", debugCommand, -2,
     "admin no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"config", configCommand, -2,
     "admin ok-loading ok-stale no-script",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"subscribe", subscribeCommand, -2,
     "pub-sub no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"unsubscribe", unsubscribeCommand, -1,
     "pub-sub no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"psubscribe", psubscribeCommand, -2,
     "pub-sub no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"punsubscribe", punsubscribeCommand, -1,
     "pub-sub no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"publish", publishCommand, 3,
     "pub-sub ok-loading ok-stale fast",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"pubsub", pubsubCommand, -2,
     "pub-sub ok-loading ok-stale random",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"watch", watchCommand, -2,
     "no-script fast ok-loading ok-stale @transaction",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"unwatch", unwatchCommand, 1,
     "no-script fast ok-loading ok-stale @transaction",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"cluster", clusterCommand, -2,
     "admin ok-stale random",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"restore", restoreCommand, -4,
     "write use-memory @keyspace @dangerous",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"restore-asking", restoreCommand, -4,
     "write use-memory cluster-asking @keyspace @dangerous",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"migrate", migrateCommand, -6,
     "write random @keyspace @dangerous",
     0, migrateGetKeys, 0, 0, 0, 0, 0, 0},

    {"asking", askingCommand, 1,
     "fast @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"readonly", readonlyCommand, 1,
     "fast @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"readwrite", readwriteCommand, 1,
     "fast @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"dump", dumpCommand, 2,
     "read-only random @keyspace",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"object", objectCommand, -2,
     "read-only random @keyspace",
     0, NULL, 2, 2, 1, 0, 0, 0},

    {"memory", memoryCommand, -2,
     "random read-only",
     0, memoryGetKeys, 0, 0, 0, 0, 0, 0},

    {"client", clientCommand, -2,
     "admin no-script random ok-loading ok-stale @connection",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"hello", helloCommand, -2,
     "no-auth no-script fast no-monitor ok-loading ok-stale no-slowlog @connection",
     0, NULL, 0, 0, 0, 0, 0, 0},

    /* EVAL can modify the dataset, however it is not flagged as a write
     * command since we do the check while running commands from Lua. */
    {"eval", evalCommand, -3,
     "no-script @scripting",
     0, evalGetKeys, 0, 0, 0, 0, 0, 0},

    {"evalsha", evalShaCommand, -3,
     "no-script @scripting",
     0, evalGetKeys, 0, 0, 0, 0, 0, 0},

    {"slowlog", slowlogCommand, -2,
     "admin random ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"script", scriptCommand, -2,
     "no-script @scripting",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"time", timeCommand, 1,
     "read-only random fast ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"bitop", bitopCommand, -4,
     "write use-memory @bitmap",
     0, NULL, 2, -1, 1, 0, 0, 0},

    {"bitcount", bitcountCommand, -2,
     "read-only @bitmap",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"bitpos", bitposCommand, -3,
     "read-only @bitmap",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"wait", waitCommand, 3,
     "no-script @keyspace",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"command", commandCommand, -1,
     "ok-loading ok-stale random @connection",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"geoadd", geoaddCommand, -5,
     "write use-memory @geo",
     0, NULL, 1, 1, 1, 0, 0, 0},

    /* GEORADIUS has store options that may write. */
    {"georadius", georadiusCommand, -6,
     "write use-memory @geo",
     0, georadiusGetKeys, 1, 1, 1, 0, 0, 0},

    {"georadius_ro", georadiusroCommand, -6,
     "read-only @geo",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"georadiusbymember", georadiusbymemberCommand, -5,
     "write use-memory @geo",
     0, georadiusGetKeys, 1, 1, 1, 0, 0, 0},

    {"georadiusbymember_ro", georadiusbymemberroCommand, -5,
     "read-only @geo",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"geohash", geohashCommand, -2,
     "read-only @geo",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"geopos", geoposCommand, -2,
     "read-only @geo",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"geodist", geodistCommand, -4,
     "read-only @geo",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"pfselftest", pfselftestCommand, 1,
     "admin @hyperloglog",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"pfadd", pfaddCommand, -2,
     "write use-memory fast @hyperloglog",
     0, NULL, 1, 1, 1, 0, 0, 0},

    /* Technically speaking PFCOUNT may change the key since it changes the
     * final bytes in the HyperLogLog representation. However in this case
     * we claim that the representation, even if accessible, is an internal
     * affair, and the command is semantically read only. */
    {"pfcount", pfcountCommand, -2,
     "read-only @hyperloglog",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"pfmerge", pfmergeCommand, -2,
     "write use-memory @hyperloglog",
     0, NULL, 1, -1, 1, 0, 0, 0},

    {"pfdebug", pfdebugCommand, -3,
     "admin write",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"xadd", xaddCommand, -5,
     "write use-memory fast random @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xrange", xrangeCommand, -4,
     "read-only @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xrevrange", xrevrangeCommand, -4,
     "read-only @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xlen", xlenCommand, 2,
     "read-only fast @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xread", xreadCommand, -4,
     "read-only @stream @blocking",
     0, xreadGetKeys, 0, 0, 0, 0, 0, 0},

    {"xreadgroup", xreadCommand, -7,
     "write @stream @blocking",
     0, xreadGetKeys, 0, 0, 0, 0, 0, 0},

    {"xgroup", xgroupCommand, -2,
     "write use-memory @stream",
     0, NULL, 2, 2, 1, 0, 0, 0},

    {"xsetid", xsetidCommand, 3,
     "write use-memory fast @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xack", xackCommand, -4,
     "write fast random @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xpending", xpendingCommand, -3,
     "read-only random @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xclaim", xclaimCommand, -6,
     "write random fast @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xinfo", xinfoCommand, -2,
     "read-only random @stream",
     0, NULL, 2, 2, 1, 0, 0, 0},

    {"xdel", xdelCommand, -3,
     "write fast @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"xtrim", xtrimCommand, -2,
     "write random @stream",
     0, NULL, 1, 1, 1, 0, 0, 0},

    {"post", securityWarningCommand, -1,
     "ok-loading ok-stale read-only",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"host:", securityWarningCommand, -1,
     "ok-loading ok-stale read-only",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"latency", latencyCommand, -2,
     "admin no-script ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"lolwut", lolwutCommand, -1,
     "read-only fast",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"acl", aclCommand, -2,
     "admin no-script no-slowlog ok-loading ok-stale",
     0, NULL, 0, 0, 0, 0, 0, 0},

    {"stralgo", stralgoCommand, -2,
     "read-only @string",
     0, lcsGetKeys, 0, 0, 0, 0, 0, 0}};

/*============================ Utility functions ============================ */

/* We use a private localtime implementation which is fork-safe. The logging
 * function of Redis may be called from other threads. */
void nolocks_localtime(struct tm *tmp, time_t t, time_t tz, int dst);

/* Low level logging. To use only for very big messages, otherwise
 * serverLog() is to prefer. */
void serverLogRaw(int level, const char *msg)
{
    const int syslogLevelMap[] = {LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING};
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & LL_RAW);
    int log_to_stdout = server.logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity)
        return;

    fp = log_to_stdout ? stdout : fopen(server.logfile, "a");
    if (!fp)
        return;

    if (rawmode)
    {
        fprintf(fp, "%s", msg);
    }
    else
    {
        int off;
        struct timeval tv;
        int role_char;
        pid_t pid = getpid();

        gettimeofday(&tv, NULL);
        struct tm tm;
        nolocks_localtime(&tm, tv.tv_sec, server.timezone, server.daylight_active);
        off = strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S.", &tm);
        snprintf(buf + off, sizeof(buf) - off, "%03d", (int)tv.tv_usec / 1000);
        if (server.sentinel_mode)
        {
            role_char = 'X'; /* Sentinel. */
        }
        else if (pid != server.pid)
        {
            role_char = 'C'; /* RDB / AOF writing child. */
        }
        else
        {
            role_char = (server.masterhost ? 'S' : 'M'); /* Slave or Master. */
        }
        fprintf(fp, "%d:%c %s %c %s\n",
                (int)getpid(), role_char, buf, c[level], msg);
    }
    fflush(fp);

    if (!log_to_stdout)
        fclose(fp);
    if (server.syslog_enabled)
        syslog(syslogLevelMap[level], "%s", msg);
}

/* Like serverLogRaw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void serverLog(int level, const char *fmt, ...)
{
    va_list ap;
    char msg[LOG_MAX_LEN];

    if ((level & 0xff) < server.verbosity)
        return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    serverLogRaw(level, msg);
}

/* Log a fixed message without printf-alike capabilities, in a way that is
 * safe to call from a signal handler.
 *
 * We actually use this only for signals that are not fatal from the point
 * of view of Redis. Signals that are going to kill the server anyway and
 * where we need printf-alike features are served by serverLog(). */
void serverLogFromHandler(int level, const char *msg)
{
    int fd;
    int log_to_stdout = server.logfile[0] == '\0';
    char buf[64];

    if ((level & 0xff) < server.verbosity || (log_to_stdout && server.daemonize))
        return;
    fd = log_to_stdout ? STDOUT_FILENO : open(server.logfile, O_APPEND | O_CREAT | O_WRONLY, 0644);
    if (fd == -1)
        return;
    ll2string(buf, sizeof(buf), getpid());
    if (write(fd, buf, strlen(buf)) == -1)
        goto err;
    if (write(fd, ":signal-handler (", 17) == -1)
        goto err;
    ll2string(buf, sizeof(buf), time(NULL));
    if (write(fd, buf, strlen(buf)) == -1)
        goto err;
    if (write(fd, ") ", 2) == -1)
        goto err;
    if (write(fd, msg, strlen(msg)) == -1)
        goto err;
    if (write(fd, "\n", 1) == -1)
        goto err;
err:
    if (!log_to_stdout)
        close(fd);
}

/* Return the UNIX time in microseconds 返回unix时间的微秒值*/
long long ustime(void)
{
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

/* Return the UNIX time in milliseconds */
mstime_t mstime(void)
{
    return ustime() / 1000;
}

/* After an RDB dump or AOF rewrite we exit from children using _exit() instead of
 * exit(), because the latter may interact with the same file objects used by
 * the parent process. However if we are testing the coverage normal exit() is
 * used in order to obtain the right coverage information. */
void exitFromChild(int retcode)
{
#ifdef COVERAGE_TEST
    exit(retcode);
#else
    _exit(retcode);
#endif
}

/*====================== Hash table type implementation  ==================== */

/* This is a hash table type that uses the SDS dynamic strings library as
 * keys and redis objects as values (objects can hold SDS strings,
 * lists, sets). */

void dictVanillaFree(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    zfree(val);
}

void dictListDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    listRelease((list *)val);
}

int dictSdsKeyCompare(void *privdata, const void *key1,
                      const void *key2)
{
    int l1, l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2)
        return 0;
    return memcmp(key1, key2, l1) == 0;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1,
                          const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

void dictObjectDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    if (val == NULL)
        return; /* Lazy freeing will set value to NULL. */
    decrRefCount(val);
}

void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

int dictObjKeyCompare(void *privdata, const void *key1,
                      const void *key2)
{
    const robj *o1 = key1, *o2 = key2;
    return dictSdsKeyCompare(privdata, o1->ptr, o2->ptr);
}

uint64_t dictObjHash(const void *key)
{
    const robj *o = key;
    return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
}

uint64_t dictSdsHash(const void *key)
{
    return dictGenHashFunction((unsigned char *)key, sdslen((char *)key));
}

uint64_t dictSdsCaseHash(const void *key)
{
    return dictGenCaseHashFunction((unsigned char *)key, sdslen((char *)key));
}

int dictEncObjKeyCompare(void *privdata, const void *key1,
                         const void *key2)
{
    robj *o1 = (robj *)key1, *o2 = (robj *)key2;
    int cmp;

    if (o1->encoding == OBJ_ENCODING_INT &&
        o2->encoding == OBJ_ENCODING_INT)
        return o1->ptr == o2->ptr;

    /* Due to OBJ_STATIC_REFCOUNT, we avoid calling getDecodedObject() without
     * good reasons, because it would incrRefCount() the object, which
     * is invalid. So we check to make sure dictFind() works with static
     * objects as well. */
    if (o1->refcount != OBJ_STATIC_REFCOUNT)
        o1 = getDecodedObject(o1);
    if (o2->refcount != OBJ_STATIC_REFCOUNT)
        o2 = getDecodedObject(o2);
    cmp = dictSdsKeyCompare(privdata, o1->ptr, o2->ptr);
    if (o1->refcount != OBJ_STATIC_REFCOUNT)
        decrRefCount(o1);
    if (o2->refcount != OBJ_STATIC_REFCOUNT)
        decrRefCount(o2);
    return cmp;
}

uint64_t dictEncObjHash(const void *key)
{
    robj *o = (robj *)key;

    if (sdsEncodedObject(o))
    {
        return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
    }
    else
    {
        if (o->encoding == OBJ_ENCODING_INT)
        {
            char buf[32];
            int len;

            len = ll2string(buf, 32, (long)o->ptr);
            return dictGenHashFunction((unsigned char *)buf, len);
        }
        else
        {
            uint64_t hash;

            o = getDecodedObject(o);
            hash = dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
            decrRefCount(o);
            return hash;
        }
    }
}

/* Generic hash table type where keys are Redis Objects, Values
 * dummy pointers. */
dictType objectKeyPointerValueDictType = {
    dictEncObjHash,       /* hash function */
    NULL,                 /* key dup */
    NULL,                 /* val dup */
    dictEncObjKeyCompare, /* key compare */
    dictObjectDestructor, /* key destructor */
    NULL                  /* val destructor */
};

/* Like objectKeyPointerValueDictType(), but values can be destroyed, if
 * not NULL, calling zfree(). */
dictType objectKeyHeapPointerValueDictType = {
    dictEncObjHash,       /* hash function */
    NULL,                 /* key dup */
    NULL,                 /* val dup */
    dictEncObjKeyCompare, /* key compare */
    dictObjectDestructor, /* key destructor */
    dictVanillaFree       /* val destructor */
};

/* Set dictionary type. Keys are SDS strings, values are not used. */
dictType setDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    dictSdsDestructor, /* key destructor */
    NULL               /* val destructor */
};

/* Sorted sets hash (note: a skiplist is used in addition to the hash table) */
dictType zsetDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    NULL,              /* Note: SDS string shared & freed by skiplist */
    NULL               /* val destructor */
};

/* Db->dict, keys are sds strings, vals are Redis objects. */
dictType dbDictType = {
    dictSdsHash,         /* hash function */
    NULL,                /* key dup */
    NULL,                /* val dup */
    dictSdsKeyCompare,   /* key compare */
    dictSdsDestructor,   /* key destructor */
    dictObjectDestructor /* val destructor */
};

/* server.lua_scripts sha (as sds string) -> scripts (as robj) cache. */
dictType shaScriptObjectDictType = {
    dictSdsCaseHash,       /* hash function */
    NULL,                  /* key dup */
    NULL,                  /* val dup */
    dictSdsKeyCaseCompare, /* key compare */
    dictSdsDestructor,     /* key destructor */
    dictObjectDestructor   /* val destructor */
};

/* Db->expires */
dictType keyptrDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    NULL,              /* key destructor */
    NULL               /* val destructor */
};

/* Command table. sds string -> command struct pointer. */
dictType commandTableDictType = {
    dictSdsCaseHash,       /* hash function */
    NULL,                  /* key dup */
    NULL,                  /* val dup */
    dictSdsKeyCaseCompare, /* key compare */
    dictSdsDestructor,     /* key destructor */
    NULL                   /* val destructor */
};

/* Hash type hash table (note that small hashes are represented with ziplists) */
dictType hashDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    dictSdsDestructor, /* key destructor */
    dictSdsDestructor  /* val destructor */
};

/* Keylist hash table type has unencoded redis objects as keys and
 * lists as values. It's used for blocking operations (BLPOP) and to
 * map swapped keys to a list of clients waiting for this keys to be loaded. */
dictType keylistDictType = {
    dictObjHash,          /* hash function */
    NULL,                 /* key dup */
    NULL,                 /* val dup */
    dictObjKeyCompare,    /* key compare */
    dictObjectDestructor, /* key destructor */
    dictListDestructor    /* val destructor */
};

/* Cluster nodes hash table, mapping nodes addresses 1.2.3.4:6379 to
 * clusterNode structures. */
dictType clusterNodesDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    dictSdsDestructor, /* key destructor */
    NULL               /* val destructor */
};

/* Cluster re-addition blacklist. This maps node IDs to the time
 * we can re-add this node. The goal is to avoid readding a removed
 * node for some time. */
dictType clusterNodesBlackListDictType = {
    dictSdsCaseHash,       /* hash function */
    NULL,                  /* key dup */
    NULL,                  /* val dup */
    dictSdsKeyCaseCompare, /* key compare */
    dictSdsDestructor,     /* key destructor */
    NULL                   /* val destructor */
};

/* Modules system dictionary type. Keys are module name,
 * values are pointer to RedisModule struct. */
dictType modulesDictType = {
    dictSdsCaseHash,       /* hash function */
    NULL,                  /* key dup */
    NULL,                  /* val dup */
    dictSdsKeyCaseCompare, /* key compare */
    dictSdsDestructor,     /* key destructor */
    NULL                   /* val destructor */
};

/* Migrate cache dict type. */
dictType migrateCacheDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    dictSdsDestructor, /* key destructor */
    NULL               /* val destructor */
};

/* Replication cached script dict (server.repl_scriptcache_dict).
 * Keys are sds SHA1 strings, while values are not used at all in the current
 * implementation. */
dictType replScriptCacheDictType = {
    dictSdsCaseHash,       /* hash function */
    NULL,                  /* key dup */
    NULL,                  /* val dup */
    dictSdsKeyCaseCompare, /* key compare */
    dictSdsDestructor,     /* key destructor */
    NULL                   /* val destructor */
};

int htNeedsResize(dict *dict)
{
    long long size, used;

    size = dictSlots(dict);
    used = dictSize(dict);
    return (size > DICT_HT_INITIAL_SIZE &&
            (used * 100 / size < HASHTABLE_MIN_FILL));
}

/* If the percentage of used slots in the HT reaches HASHTABLE_MIN_FILL
 * we resize the hash table to save memory */
void tryResizeHashTables(int dbid)
{
    if (htNeedsResize(server.db[dbid].dict))
        dictResize(server.db[dbid].dict);
    if (htNeedsResize(server.db[dbid].expires))
        dictResize(server.db[dbid].expires);
}

/* Our hash table implementation performs rehashing incrementally while
 * we write/read from the hash table. Still if the server is idle, the hash
 * table will use two tables for a long time. So we try to use 1 millisecond
 * of CPU time at every call of this function to perform some rehashing.
 *
 * The function returns 1 if some rehashing was performed, otherwise 0
 * is returned. */
int incrementallyRehash(int dbid)
{
    /* Keys dictionary */
    if (dictIsRehashing(server.db[dbid].dict))
    {
        dictRehashMilliseconds(server.db[dbid].dict, 1);
        return 1; /* already used our millisecond for this loop... */
    }
    /* Expires */
    if (dictIsRehashing(server.db[dbid].expires))
    {
        dictRehashMilliseconds(server.db[dbid].expires, 1);
        return 1; /* already used our millisecond for this loop... */
    }
    return 0;
}

/* This function is called once a background process of some kind terminates,
 * as we want to avoid resizing the hash tables when there is a child in order
 * to play well with copy-on-write (otherwise when a resize happens lots of
 * memory pages are copied). The goal of this function is to update the ability
 * for dict.c to resize the hash tables accordingly to the fact we have an
 * active fork child running. */
void updateDictResizePolicy(void)
{
    if (!hasActiveChildProcess())
        dictEnableResize();
    else
        dictDisableResize();
}

/* Return true if there are no active children processes doing RDB saving,
 * AOF rewriting, or some side process spawned by a loaded module. */
int hasActiveChildProcess()
{
    return server.rdb_child_pid != -1 ||
           server.aof_child_pid != -1 ||
           server.module_child_pid != -1;
}

/* Return true if this instance has persistence completely turned off:
 * both RDB and AOF are disabled. */
int allPersistenceDisabled(void)
{
    return server.saveparamslen == 0 && server.aof_state == AOF_OFF;
}

/* ======================= Cron: called every 100 ms ======================== */

/* Add a sample to the operations per second array of samples. */
void trackInstantaneousMetric(int metric, long long current_reading)
{
    long long t = mstime() - server.inst_metric[metric].last_sample_time;
    long long ops = current_reading -
                    server.inst_metric[metric].last_sample_count;
    long long ops_sec;

    ops_sec = t > 0 ? (ops * 1000 / t) : 0;

    server.inst_metric[metric].samples[server.inst_metric[metric].idx] =
        ops_sec;
    server.inst_metric[metric].idx++;
    server.inst_metric[metric].idx %= STATS_METRIC_SAMPLES;
    server.inst_metric[metric].last_sample_time = mstime();
    server.inst_metric[metric].last_sample_count = current_reading;
}

/* Return the mean of all the samples. */
long long getInstantaneousMetric(int metric)
{
    int j;
    long long sum = 0;

    for (j = 0; j < STATS_METRIC_SAMPLES; j++)
        sum += server.inst_metric[metric].samples[j];
    return sum / STATS_METRIC_SAMPLES;
}

/* The client query buffer is an sds.c string that can end with a lot of
 * free space not used, this function reclaims space if needed.
 *
 * The function always returns 0 as it never terminates the client. */
int clientsCronResizeQueryBuffer(client *c)
{
    size_t querybuf_size = sdsAllocSize(c->querybuf);
    time_t idletime = server.unixtime - c->lastinteraction;

    /* There are two conditions to resize the query buffer:
     * 1) Query buffer is > BIG_ARG and too big for latest peak.
     * 2) Query buffer is > BIG_ARG and client is idle. */
    if (querybuf_size > PROTO_MBULK_BIG_ARG &&
        ((querybuf_size / (c->querybuf_peak + 1)) > 2 ||
         idletime > 2))
    {
        /* Only resize the query buffer if it is actually wasting
         * at least a few kbytes. */
        if (sdsavail(c->querybuf) > 1024 * 4)
        {
            c->querybuf = sdsRemoveFreeSpace(c->querybuf);
        }
    }
    /* Reset the peak again to capture the peak memory usage in the next
     * cycle. */
    c->querybuf_peak = 0;

    /* Clients representing masters also use a "pending query buffer" that
     * is the yet not applied part of the stream we are reading. Such buffer
     * also needs resizing from time to time, otherwise after a very large
     * transfer (a huge value or a big MIGRATE operation) it will keep using
     * a lot of memory. */
    if (c->flags & CLIENT_MASTER)
    {
        /* There are two conditions to resize the pending query buffer:
         * 1) Pending Query buffer is > LIMIT_PENDING_QUERYBUF.
         * 2) Used length is smaller than pending_querybuf_size/2 */
        size_t pending_querybuf_size = sdsAllocSize(c->pending_querybuf);
        if (pending_querybuf_size > LIMIT_PENDING_QUERYBUF &&
            sdslen(c->pending_querybuf) < (pending_querybuf_size / 2))
        {
            c->pending_querybuf = sdsRemoveFreeSpace(c->pending_querybuf);
        }
    }
    return 0;
}

/* This function is used in order to track clients using the biggest amount
 * of memory in the latest few seconds. This way we can provide such information
 * in the INFO output (clients section), without having to do an O(N) scan for
 * all the clients.
 *
 * This is how it works. We have an array of CLIENTS_PEAK_MEM_USAGE_SLOTS slots
 * where we track, for each, the biggest client output and input buffers we
 * saw in that slot. Every slot correspond to one of the latest seconds, since
 * the array is indexed by doing UNIXTIME % CLIENTS_PEAK_MEM_USAGE_SLOTS.
 *
 * When we want to know what was recently the peak memory usage, we just scan
 * such few slots searching for the maximum value. */
#define CLIENTS_PEAK_MEM_USAGE_SLOTS 8
size_t ClientsPeakMemInput[CLIENTS_PEAK_MEM_USAGE_SLOTS];
size_t ClientsPeakMemOutput[CLIENTS_PEAK_MEM_USAGE_SLOTS];

int clientsCronTrackExpansiveClients(client *c)
{
    size_t in_usage = sdsZmallocSize(c->querybuf) + c->argv_len_sum;
    size_t out_usage = getClientOutputBufferMemoryUsage(c);
    int i = server.unixtime % CLIENTS_PEAK_MEM_USAGE_SLOTS;
    int zeroidx = (i + 1) % CLIENTS_PEAK_MEM_USAGE_SLOTS;

    /* Always zero the next sample, so that when we switch to that second, we'll
     * only register samples that are greater in that second without considering
     * the history of such slot.
     *
     * Note: our index may jump to any random position if serverCron() is not
     * called for some reason with the normal frequency, for instance because
     * some slow command is called taking multiple seconds to execute. In that
     * case our array may end containing data which is potentially older
     * than CLIENTS_PEAK_MEM_USAGE_SLOTS seconds: however this is not a problem
     * since here we want just to track if "recently" there were very expansive
     * clients from the POV of memory usage. */
    ClientsPeakMemInput[zeroidx] = 0;
    ClientsPeakMemOutput[zeroidx] = 0;

    /* Track the biggest values observed so far in this slot. */
    if (in_usage > ClientsPeakMemInput[i])
        ClientsPeakMemInput[i] = in_usage;
    if (out_usage > ClientsPeakMemOutput[i])
        ClientsPeakMemOutput[i] = out_usage;

    return 0; /* This function never terminates the client. */
}

/* Iterating all the clients in getMemoryOverheadData() is too slow and
 * in turn would make the INFO command too slow. So we perform this
 * computation incrementally and track the (not instantaneous but updated
 * to the second) total memory used by clients using clinetsCron() in
 * a more incremental way (depending on server.hz). */
int clientsCronTrackClientsMemUsage(client *c)
{
    size_t mem = 0;
    int type = getClientType(c);
    mem += getClientOutputBufferMemoryUsage(c);
    mem += sdsZmallocSize(c->querybuf);
    mem += zmalloc_size(c);
    mem += c->argv_len_sum;
    if (c->argv)
        mem += zmalloc_size(c->argv);
    /* Now that we have the memory used by the client, remove the old
     * value from the old category, and add it back. */
    server.stat_clients_type_memory[c->client_cron_last_memory_type] -=
        c->client_cron_last_memory_usage;
    server.stat_clients_type_memory[type] += mem;
    /* Remember what we added and where, to remove it next time. */
    c->client_cron_last_memory_usage = mem;
    c->client_cron_last_memory_type = type;
    return 0;
}

/* Return the max samples in the memory usage of clients tracked by
 * the function clientsCronTrackExpansiveClients(). */
void getExpansiveClientsInfo(size_t *in_usage, size_t *out_usage)
{
    size_t i = 0, o = 0;
    for (int j = 0; j < CLIENTS_PEAK_MEM_USAGE_SLOTS; j++)
    {
        if (ClientsPeakMemInput[j] > i)
            i = ClientsPeakMemInput[j];
        if (ClientsPeakMemOutput[j] > o)
            o = ClientsPeakMemOutput[j];
    }
    *in_usage = i;
    *out_usage = o;
}

/* This function is called by serverCron() and is used in order to perform
 * operations on clients that are important to perform constantly. For instance
 * we use this function in order to disconnect clients after a timeout, including
 * clients blocked in some blocking command with a non-zero timeout.
 *
 * The function makes some effort to process all the clients every second, even
 * if this cannot be strictly guaranteed, since serverCron() may be called with
 * an actual frequency lower than server.hz in case of latency events like slow
 * commands.
 *
 * It is very important for this function, and the functions it calls, to be
 * very fast: sometimes Redis has tens of hundreds of connected clients, and the
 * default server.hz value is 10, so sometimes here we need to process thousands
 * of clients per second, turning this function into a source of latency.
 */
#define CLIENTS_CRON_MIN_ITERATIONS 5
void clientsCron(void)
{
    /* Try to process at least numclients/server.hz of clients
     * per call. Since normally (if there are no big latency events) this
     * function is called server.hz times per second, in the average case we
     * process all the clients in 1 second. */
    int numclients = listLength(server.clients);
    int iterations = numclients / server.hz;
    mstime_t now = mstime();

    /* Process at least a few clients while we are at it, even if we need
     * to process less than CLIENTS_CRON_MIN_ITERATIONS to meet our contract
     * of processing each client once per second. */
    if (iterations < CLIENTS_CRON_MIN_ITERATIONS)
        iterations = (numclients < CLIENTS_CRON_MIN_ITERATIONS) ? numclients : CLIENTS_CRON_MIN_ITERATIONS;

    while (listLength(server.clients) && iterations--)
    {
        client *c;
        listNode *head;

        /* Rotate the list, take the current head, process.
         * This way if the client must be removed from the list it's the
         * first element and we don't incur into O(N) computation. */
        listRotateTailToHead(server.clients);
        head = listFirst(server.clients);
        c = listNodeValue(head);
        /* The following functions do different service checks on the client.
         * The protocol is that they return non-zero if the client was
         * terminated. */
        if (clientsCronHandleTimeout(c, now))
            continue;
        if (clientsCronResizeQueryBuffer(c))
            continue;
        if (clientsCronTrackExpansiveClients(c))
            continue;
        if (clientsCronTrackClientsMemUsage(c))
            continue;
    }
}

/* This function handles 'background' operations we are required to do
 * incrementally in Redis databases, such as active key expiring, resizing,
 * rehashing. */
void databasesCron(void)
{
    /* Expire keys by random sampling. Not required for slaves
     * as master will synthesize DELs for us. */
    if (server.active_expire_enabled)
    {
        if (iAmMaster())
        {
            activeExpireCycle(ACTIVE_EXPIRE_CYCLE_SLOW);
        }
        else
        {
            expireSlaveKeys();
        }
    }

    /* Defrag keys gradually. */
    activeDefragCycle();

    /* Perform hash tables rehashing if needed, but only if there are no
     * other processes saving the DB on disk. Otherwise rehashing is bad
     * as will cause a lot of copy-on-write of memory pages. */
    if (!hasActiveChildProcess())
    {
        /* We use global counters so if we stop the computation at a given
         * DB we'll be able to start from the successive in the next
         * cron loop iteration. */
        static unsigned int resize_db = 0;
        static unsigned int rehash_db = 0;
        int dbs_per_call = CRON_DBS_PER_CALL;
        int j;

        /* Don't test more DBs than we have. */
        if (dbs_per_call > server.dbnum)
            dbs_per_call = server.dbnum;

        /* Resize */
        for (j = 0; j < dbs_per_call; j++)
        {
            tryResizeHashTables(resize_db % server.dbnum);
            resize_db++;
        }

        /* Rehash */
        if (server.activerehashing)
        {
            for (j = 0; j < dbs_per_call; j++)
            {
                int work_done = incrementallyRehash(rehash_db);
                if (work_done)
                {
                    /* If the function did some work, stop here, we'll do
                     * more at the next cron loop. */
                    break;
                }
                else
                {
                    /* If this db didn't need rehash, we'll try the next one. */
                    rehash_db++;
                    rehash_db %= server.dbnum;
                }
            }
        }
    }
}

/* We take a cached value of the unix time in the global state because with
 * virtual memory and aging there is to store the current time in objects at
 * every object access, and accuracy is not needed. To access a global var is
 * a lot faster than calling time(NULL).
 * 我们在全局状态下获取unix时间的缓存值，因为随着虚拟内存和老化，
 * 每次对象访问时都会将当前时间存储在对象中，并且不需要准确性。 访问全局变量比调用 time(NULL) 快得多。
 * This function should be fast because it is called at every command execution
 * in call(), so it is possible to decide if to update the daylight saving
 * info or not using the 'update_daylight_info' argument. Normally we update
 * such info only when calling this function from serverCron() but not when
 * calling it from call().
 * 这个函数应该很快，因为它在 call() 中的每个命令执行时被调用，
 * 因此可以决定是否更新夏令时信息或不使用 'update_daylight_info' 参数。
 * 通常我们仅在从 serverCron() 调用此函数时更新此类信息，
 * 而不是在从 call() 调用它时更新此类信息。*/
void updateCachedTime(int update_daylight_info)
{
    server.ustime = ustime();
    server.mstime = server.ustime / 1000;
    server.unixtime = server.mstime / 1000;

    /* To get information about daylight saving time, we need to call
     * localtime_r and cache the result. However calling localtime_r in this
     * context is safe since we will never fork() while here, in the main
     * thread. The logging function will call a thread safe version of
     * localtime that has no locks.
     * 要获取有关夏令时的信息，我们需要调用 localtime_r 并缓存结果。
     * 但是在这种情况下调用 localtime_r 是安全的，
     * 因为我们永远不会在主线程中 fork() 。
     * 日志记录函数将调用没有锁的本地时间线程安全版本*/
    if (update_daylight_info)
    {
        struct tm tm;
        time_t ut = server.unixtime;
        localtime_r(&ut, &tm);
        server.daylight_active = tm.tm_isdst;
    }
}

void checkChildrenDone(void)
{
    int statloc;
    pid_t pid;

    if ((pid = wait3(&statloc, WNOHANG, NULL)) != 0)
    {
        int exitcode = WEXITSTATUS(statloc);
        int bysignal = 0;

        if (WIFSIGNALED(statloc))
            bysignal = WTERMSIG(statloc);

        /* sigKillChildHandler catches the signal and calls exit(), but we
         * must make sure not to flag lastbgsave_status, etc incorrectly.
         * We could directly terminate the child process via SIGUSR1
         * without handling it, but in this case Valgrind will log an
         * annoying error. */
        if (exitcode == SERVER_CHILD_NOERROR_RETVAL)
        {
            bysignal = SIGUSR1;
            exitcode = 1;
        }

        if (pid == -1)
        {
            serverLog(LL_WARNING, "wait3() returned an error: %s. "
                                  "rdb_child_pid = %d, aof_child_pid = %d, module_child_pid = %d",
                      strerror(errno),
                      (int)server.rdb_child_pid,
                      (int)server.aof_child_pid,
                      (int)server.module_child_pid);
        }
        else if (pid == server.rdb_child_pid)
        {
            backgroundSaveDoneHandler(exitcode, bysignal);
            if (!bysignal && exitcode == 0)
                receiveChildInfo();
        }
        else if (pid == server.aof_child_pid)
        {
            backgroundRewriteDoneHandler(exitcode, bysignal);
            if (!bysignal && exitcode == 0)
                receiveChildInfo();
        }
        else if (pid == server.module_child_pid)
        {
            ModuleForkDoneHandler(exitcode, bysignal);
            if (!bysignal && exitcode == 0)
                receiveChildInfo();
        }
        else
        {
            if (!ldbRemoveChild(pid))
            {
                serverLog(LL_WARNING,
                          "Warning, detected child with unmatched pid: %ld",
                          (long)pid);
            }
        }
        updateDictResizePolicy();
        closeChildInfoPipe();
    }
}

/* This is our timer interrupt, called server.hz times per second.
 * Here is where we do a number of things that need to be done asynchronously.
 * For instance:
 * 这是我们是时间中断处理器，每一秒根据server.hz次数被调用
 * 这是一些我们需要去处理的异步时间，例如：
 *
 * - Active expired keys collection (it is also performed in a lazy way on
 *   lookup).
 * - Software watchdog.
 * - Update some statistic.
 * - Incremental rehashing of the DBs hash tables.
 * - Triggering BGSAVE / AOF rewrite, and handling of terminated children.
 * - Clients timeout of different kinds.
 * - Replication reconnection.
 * - Many more...
 *
 * - 激活过期键收集（它也在查找时以惰性方式执行）
 * - 软件看门狗
 * - 更新统计信息
 * - DBs 哈希表的增量重新散列。
 * - 触发 BGSAVE/AOF 写入 和处理子程序退出
 * - 客户端不同种类的超时
 * - 复制重连接
 * - 更多其他的东西
 *
 * Everything directly called here will be called server.hz times per second,
 * so in order to throttle execution of things we want to do less frequently
 * a macro is used: run_with_period(milliseconds) { .... }
 *
 * 这里的内容会被每秒调用server.hz次
 * 因此，为了限制我们不想经常做的事情的执行，可以做一个小的设置run_with_period(milliseconds) { .... }
 * 以每milliseconds毫秒调用
 *
 */
//被定期调用（根据`server.hz`频率）并执行需要时不时执行的任务，例如定期检查客户端是否连接超时
int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
    int j;
    UNUSED(eventLoop);
    UNUSED(id);
    UNUSED(clientData);

    /* Software watchdog: deliver the SIGALRM that will reach the signal
     * handler if we don't return here fast enough. */
    if (server.watchdog_period)
        watchdogScheduleSignal(server.watchdog_period);

    /* Update the time cache. */
    updateCachedTime(1);

    server.hz = server.config_hz;
    /* Adapt the server.hz value to the number of configured clients. If we have
     * many clients, we want to call serverCron() with an higher frequency. */
    if (server.dynamic_hz)
    {
        while (listLength(server.clients) / server.hz >
               MAX_CLIENTS_PER_CLOCK_TICK)
        {
            server.hz *= 2;
            if (server.hz > CONFIG_MAX_HZ)
            {
                server.hz = CONFIG_MAX_HZ;
                break;
            }
        }
    }

    run_with_period(100)
    {
        trackInstantaneousMetric(STATS_METRIC_COMMAND, server.stat_numcommands);
        trackInstantaneousMetric(STATS_METRIC_NET_INPUT,
                                 server.stat_net_input_bytes);
        trackInstantaneousMetric(STATS_METRIC_NET_OUTPUT,
                                 server.stat_net_output_bytes);
    }

    /* We have just LRU_BITS bits per object for LRU information.
     * So we use an (eventually wrapping) LRU clock.
     *
     * Note that even if the counter wraps it's not a big problem,
     * everything will still work but some object will appear younger
     * to Redis. However for this to happen a given object should never be
     * touched for all the time needed to the counter to wrap, which is
     * not likely.
     *
     * Note that you can change the resolution altering the
     * LRU_CLOCK_RESOLUTION define. */
    server.lruclock = getLRUClock();

    /* Record the max memory used since the server was started. */
    if (zmalloc_used_memory() > server.stat_peak_memory)
        server.stat_peak_memory = zmalloc_used_memory();

    run_with_period(100)
    {
        /* Sample the RSS and other metrics here since this is a relatively slow call.
         * We must sample the zmalloc_used at the same time we take the rss, otherwise
         * the frag ratio calculate may be off (ratio of two samples at different times) */
        server.cron_malloc_stats.process_rss = zmalloc_get_rss();
        server.cron_malloc_stats.zmalloc_used = zmalloc_used_memory();
        /* Sampling the allcator info can be slow too.
         * The fragmentation ratio it'll show is potentically more accurate
         * it excludes other RSS pages such as: shared libraries, LUA and other non-zmalloc
         * allocations, and allocator reserved pages that can be pursed (all not actual frag) */
        zmalloc_get_allocator_info(&server.cron_malloc_stats.allocator_allocated,
                                   &server.cron_malloc_stats.allocator_active,
                                   &server.cron_malloc_stats.allocator_resident);
        /* in case the allocator isn't providing these stats, fake them so that
         * fragmention info still shows some (inaccurate metrics) */
        if (!server.cron_malloc_stats.allocator_resident)
        {
            /* LUA memory isn't part of zmalloc_used, but it is part of the process RSS,
             * so we must desuct it in order to be able to calculate correct
             * "allocator fragmentation" ratio */
            size_t lua_memory = lua_gc(server.lua, LUA_GCCOUNT, 0) * 1024LL;
            server.cron_malloc_stats.allocator_resident = server.cron_malloc_stats.process_rss - lua_memory;
        }
        if (!server.cron_malloc_stats.allocator_active)
            server.cron_malloc_stats.allocator_active = server.cron_malloc_stats.allocator_resident;
        if (!server.cron_malloc_stats.allocator_allocated)
            server.cron_malloc_stats.allocator_allocated = server.cron_malloc_stats.zmalloc_used;
    }

    /* We received a SIGTERM, shutting down here in a safe way, as it is
     * not ok doing so inside the signal handler. */
    if (server.shutdown_asap)
    {
        if (prepareForShutdown(SHUTDOWN_NOFLAGS) == C_OK)
            exit(0);
        serverLog(LL_WARNING, "SIGTERM received but errors trying to shut down the server, check the logs for more information");
        server.shutdown_asap = 0;
    }

    /* Show some info about non-empty databases */
    run_with_period(5000)
    {
        for (j = 0; j < server.dbnum; j++)
        {
            long long size, used, vkeys;

            size = dictSlots(server.db[j].dict);
            used = dictSize(server.db[j].dict);
            vkeys = dictSize(server.db[j].expires);
            if (used || vkeys)
            {
                serverLog(LL_VERBOSE, "DB %d: %lld keys (%lld volatile) in %lld slots HT.", j, used, vkeys, size);
                /* dictPrintStats(server.dict); */
            }
        }
    }

    /* Show information about connected clients */
    if (!server.sentinel_mode)
    {
        run_with_period(5000)
        {
            serverLog(LL_DEBUG,
                      "%lu clients connected (%lu replicas), %zu bytes in use",
                      listLength(server.clients) - listLength(server.slaves),
                      listLength(server.slaves),
                      zmalloc_used_memory());
        }
    }

    /* We need to do a few operations on clients asynchronously. */
    clientsCron();

    /* Handle background operations on Redis databases. */
    databasesCron();

    /* Start a scheduled AOF rewrite if this was requested by the user while
     * a BGSAVE was in progress. */
    if (!hasActiveChildProcess() &&
        server.aof_rewrite_scheduled)
    {
        rewriteAppendOnlyFileBackground();
    }

    /* Check if a background saving or AOF rewrite in progress terminated. */
    if (hasActiveChildProcess() || ldbPendingChildren())
    {
        checkChildrenDone();
    }
    else
    {
        /* If there is not a background saving/rewrite in progress check if
         * we have to save/rewrite now. */
        for (j = 0; j < server.saveparamslen; j++)
        {
            struct saveparam *sp = server.saveparams + j;

            /* Save if we reached the given amount of changes,
             * the given amount of seconds, and if the latest bgsave was
             * successful or if, in case of an error, at least
             * CONFIG_BGSAVE_RETRY_DELAY seconds already elapsed. */
            if (server.dirty >= sp->changes &&
                server.unixtime - server.lastsave > sp->seconds &&
                (server.unixtime - server.lastbgsave_try >
                     CONFIG_BGSAVE_RETRY_DELAY ||
                 server.lastbgsave_status == C_OK))
            {
                serverLog(LL_NOTICE, "%d changes in %d seconds. Saving...",
                          sp->changes, (int)sp->seconds);
                rdbSaveInfo rsi, *rsiptr;
                rsiptr = rdbPopulateSaveInfo(&rsi);
                rdbSaveBackground(server.rdb_filename, rsiptr);
                break;
            }
        }

        /* Trigger an AOF rewrite if needed. */
        if (server.aof_state == AOF_ON &&
            !hasActiveChildProcess() &&
            server.aof_rewrite_perc &&
            server.aof_current_size > server.aof_rewrite_min_size)
        {
            long long base = server.aof_rewrite_base_size ? server.aof_rewrite_base_size : 1;
            long long growth = (server.aof_current_size * 100 / base) - 100;
            if (growth >= server.aof_rewrite_perc)
            {
                serverLog(LL_NOTICE, "Starting automatic rewriting of AOF on %lld%% growth", growth);
                rewriteAppendOnlyFileBackground();
            }
        }
    }
    /* Just for the sake of defensive programming, to avoid forgeting to
     * call this function when need. */
    updateDictResizePolicy();

    /* AOF postponed flush: Try at every cron cycle if the slow fsync
     * completed. */
    if (server.aof_flush_postponed_start)
        flushAppendOnlyFile(0);

    /* AOF write errors: in this case we have a buffer to flush as well and
     * clear the AOF error in case of success to make the DB writable again,
     * however to try every second is enough in case of 'hz' is set to
     * a higher frequency. */
    run_with_period(1000)
    {
        if (server.aof_last_write_status == C_ERR)
            flushAppendOnlyFile(0);
    }

    /* Clear the paused clients flag if needed. */
    clientsArePaused(); /* Don't check return value, just use the side effect.*/

    /* Replication cron function -- used to reconnect to master,
     * detect transfer failures, start background RDB transfers and so forth. */
    run_with_period(1000) replicationCron();

    /* Run the Redis Cluster cron. */
    run_with_period(100)
    {
        if (server.cluster_enabled)
            clusterCron();
    }

    /* Run the Sentinel timer if we are in sentinel mode. */
    if (server.sentinel_mode)
        sentinelTimer();

    /* Cleanup expired MIGRATE cached sockets. */
    run_with_period(1000)
    {
        migrateCloseTimedoutSockets();
    }

    /* Stop the I/O threads if we don't have enough pending work. */
    stopThreadedIOIfNeeded();

    /* Resize tracking keys table if needed. This is also done at every
     * command execution, but we want to be sure that if the last command
     * executed changes the value via CONFIG SET, the server will perform
     * the operation even if completely idle. */
    if (server.tracking_clients)
        trackingLimitUsedSlots();

    /* Start a scheduled BGSAVE if the corresponding flag is set. This is
     * useful when we are forced to postpone a BGSAVE because an AOF
     * rewrite is in progress.
     *
     * Note: this code must be after the replicationCron() call above so
     * make sure when refactoring this file to keep this order. This is useful
     * because we want to give priority to RDB savings for replication. */
    if (!hasActiveChildProcess() &&
        server.rdb_bgsave_scheduled &&
        (server.unixtime - server.lastbgsave_try > CONFIG_BGSAVE_RETRY_DELAY ||
         server.lastbgsave_status == C_OK))
    {
        rdbSaveInfo rsi, *rsiptr;
        rsiptr = rdbPopulateSaveInfo(&rsi);
        if (rdbSaveBackground(server.rdb_filename, rsiptr) == C_OK)
            server.rdb_bgsave_scheduled = 0;
    }

    /* Fire the cron loop modules event. */
    RedisModuleCronLoopV1 ei = {REDISMODULE_CRON_LOOP_VERSION, server.hz};
    moduleFireServerEvent(REDISMODULE_EVENT_CRON_LOOP,
                          0,
                          &ei);

    server.cronloops++;
    return 1000 / server.hz;
}

extern int ProcessingEventsWhileBlocked;

/* This function gets called every time Redis is entering the
 * main loop of the event driven library, that is, before to sleep
 * for ready file descriptors.
 *
 * Note: This function is (currently) called from two functions:
 * 1. aeMain - The main server loop
 * 2. processEventsWhileBlocked - Process clients during RDB/AOF load
 *
 * If it was called from processEventsWhileBlocked we don't want
 * to perform all actions (For example, we don't want to expire
 * keys), but we do need to perform some actions.
 *
 * The most important is freeClientsInAsyncFreeQueue but we also
 * call some other low-risk functions. */
void beforeSleep(struct aeEventLoop *eventLoop)
{
    UNUSED(eventLoop);

    size_t zmalloc_used = zmalloc_used_memory();
    if (zmalloc_used > server.stat_peak_memory)
        server.stat_peak_memory = zmalloc_used;

    /* Just call a subset of vital functions in case we are re-entering
     * the event loop from processEventsWhileBlocked(). Note that in this
     * case we keep track of the number of events we are processing, since
     * processEventsWhileBlocked() wants to stop ASAP if there are no longer
     * events to handle. */
    if (ProcessingEventsWhileBlocked)
    {
        uint64_t processed = 0;
        processed += handleClientsWithPendingReadsUsingThreads();
        processed += tlsProcessPendingData();
        processed += handleClientsWithPendingWrites();
        processed += freeClientsInAsyncFreeQueue();
        server.events_processed_while_blocked += processed;
        return;
    }

    /* Handle precise timeouts of blocked clients. 处理被阻塞客户端的精确超时*/
    handleBlockedClientsTimeout();

    /* We should handle pending reads clients ASAP after event loop. 处理挂起的读取客户端*/
    handleClientsWithPendingReadsUsingThreads();

    /* Handle TLS pending data. 处理 TLS 待处理数据 (must be done before flushAppendOnlyFile) */
    tlsProcessPendingData();

    /* If tls still has pending unread data don't sleep at all. */
    aeSetDontWait(server.el, tlsHasPendingData());

    /* Call the Redis Cluster before sleep function. Note that this function
     * may change the state of Redis Cluster (from ok to fail or vice versa),
     * so it's a good idea to call it before serving the unblocked clients
     * later in this function.
     * 在 sleep 函数之前调用 Redis Cluster。
     * 请注意，此函数可能会更改 Redis 集群的状态（从 ok 变为失败，反之亦然），
     * 因此在此函数稍后为未阻塞的客户端提供服务之前调用它是个好主意 */
    if (server.cluster_enabled)
        clusterBeforeSleep();

    /* Run a fast expire cycle (the called function will return
     * ASAP if a fast cycle is not needed). 
     运行快速过期循环（如果不需要快速循环，被调用函数将尽快返回）*/
    if (server.active_expire_enabled && server.masterhost == NULL)
        activeExpireCycle(ACTIVE_EXPIRE_CYCLE_FAST);

    /* Unblock all the clients blocked for synchronous replication
     * in WAIT. 在 WAIT 中解除对同步复制阻塞的所有客户端 */
    if (listLength(server.clients_waiting_acks))
        processClientsWaitingReplicas();

    /* Check if there are clients unblocked by modules that implement
     * blocking commands. 检查是否有客户端被实现阻塞命令的模块解除阻塞*/
    if (moduleCount())
        moduleHandleBlockedClients();

    /* Try to process pending commands for clients that were just unblocked. 尝试为刚刚解锁的客户端处理挂起的命令。*/
    if (listLength(server.unblocked_clients))
        processUnblockedClients();

    /* Send all the slaves an ACK request if at least one client blocked
     * during the previous event loop iteration. Note that we do this after
     * processUnblockedClients(), so if there are multiple pipelined WAITs
     * and the just unblocked WAIT gets blocked again, we don't have to wait
     * a server cron cycle in absence of other event loop events. See #6623. */
    if (server.get_ack_from_slaves)
    {
        robj *argv[3];

        argv[0] = createStringObject("REPLCONF", 8);
        argv[1] = createStringObject("GETACK", 6);
        argv[2] = createStringObject("*", 1); /* Not used argument. */
        replicationFeedSlaves(server.slaves, server.slaveseldb, argv, 3);
        decrRefCount(argv[0]);
        decrRefCount(argv[1]);
        decrRefCount(argv[2]);
        server.get_ack_from_slaves = 0;
    }

    /* Send the invalidation messages to clients participating to the
     * client side caching protocol in broadcasting (BCAST) mode.
     以广播 (BCAST) 模式将失效消息发送给参与客户端缓存协议的客户端。 */
    trackingBroadcastInvalidationMessages();

    /* Write the AOF buffer on disk 将 AOF 缓冲区写入磁盘*/
    flushAppendOnlyFile(0);

    /* Handle writes with pending output buffers. */
    handleClientsWithPendingWritesUsingThreads();

    /* Close clients that need to be closed asynchronous */
    freeClientsInAsyncFreeQueue();

    /* Try to process blocked clients every once in while. Example: A module
     * calls RM_SignalKeyAsReady from within a timer callback (So we don't
     * visit processCommand() at all).
     * 尝试每隔一段时间处理一次被阻塞的客户端。
       示例：模块从计时器回调中调用 RM_SignalKeyAsReady
      （所以我们根本不访问 processCommand() ）。 */
    handleClientsBlockedOnKeys();

    /* Before we are going to sleep, let the threads access the dataset by
     * releasing the GIL. Redis main thread will not touch anything at this
     * time. */
    if (moduleCount())
        moduleReleaseGIL();

    /* Do NOT add anything below moduleReleaseGIL !!! */
}

/* This function is called immediately after the event loop multiplexing
 * API returned, and the control is going to soon return to Redis by invoking
 * the different events callbacks. */
void afterSleep(struct aeEventLoop *eventLoop)
{
    UNUSED(eventLoop);

    /* Do NOT add anything above moduleAcquireGIL !!! */

    /* Aquire the modules GIL so that their threads won't touch anything. */
    if (!ProcessingEventsWhileBlocked)
    {
        if (moduleCount())
            moduleAcquireGIL();
    }
}

/* =========================== Server initialization ======================== */

void createSharedObjects(void)
{
    int j;

    shared.crlf = createObject(OBJ_STRING, sdsnew("\r\n"));
    shared.ok = createObject(OBJ_STRING, sdsnew("+OK\r\n"));
    shared.err = createObject(OBJ_STRING, sdsnew("-ERR\r\n"));
    shared.emptybulk = createObject(OBJ_STRING, sdsnew("$0\r\n\r\n"));
    shared.czero = createObject(OBJ_STRING, sdsnew(":0\r\n"));
    shared.cone = createObject(OBJ_STRING, sdsnew(":1\r\n"));
    shared.emptyarray = createObject(OBJ_STRING, sdsnew("*0\r\n"));
    shared.pong = createObject(OBJ_STRING, sdsnew("+PONG\r\n"));
    shared.queued = createObject(OBJ_STRING, sdsnew("+QUEUED\r\n"));
    shared.emptyscan = createObject(OBJ_STRING, sdsnew("*2\r\n$1\r\n0\r\n*0\r\n"));
    shared.wrongtypeerr = createObject(OBJ_STRING, sdsnew(
                                                       "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"));
    shared.nokeyerr = createObject(OBJ_STRING, sdsnew(
                                                   "-ERR no such key\r\n"));
    shared.syntaxerr = createObject(OBJ_STRING, sdsnew(
                                                    "-ERR syntax error\r\n"));
    shared.sameobjecterr = createObject(OBJ_STRING, sdsnew(
                                                        "-ERR source and destination objects are the same\r\n"));
    shared.outofrangeerr = createObject(OBJ_STRING, sdsnew(
                                                        "-ERR index out of range\r\n"));
    shared.noscripterr = createObject(OBJ_STRING, sdsnew(
                                                      "-NOSCRIPT No matching script. Please use EVAL.\r\n"));
    shared.loadingerr = createObject(OBJ_STRING, sdsnew(
                                                     "-LOADING Redis is loading the dataset in memory\r\n"));
    shared.slowscripterr = createObject(OBJ_STRING, sdsnew(
                                                        "-BUSY Redis is busy running a script. You can only call SCRIPT KILL or SHUTDOWN NOSAVE.\r\n"));
    shared.masterdownerr = createObject(OBJ_STRING, sdsnew(
                                                        "-MASTERDOWN Link with MASTER is down and replica-serve-stale-data is set to 'no'.\r\n"));
    shared.bgsaveerr = createObject(OBJ_STRING, sdsnew(
                                                    "-MISCONF Redis is configured to save RDB snapshots, but it is currently not able to persist on disk. Commands that may modify the data set are disabled, because this instance is configured to report errors during writes if RDB snapshotting fails (stop-writes-on-bgsave-error option). Please check the Redis logs for details about the RDB error.\r\n"));
    shared.roslaveerr = createObject(OBJ_STRING, sdsnew(
                                                     "-READONLY You can't write against a read only replica.\r\n"));
    shared.noautherr = createObject(OBJ_STRING, sdsnew(
                                                    "-NOAUTH Authentication required.\r\n"));
    shared.oomerr = createObject(OBJ_STRING, sdsnew(
                                                 "-OOM command not allowed when used memory > 'maxmemory'.\r\n"));
    shared.execaborterr = createObject(OBJ_STRING, sdsnew(
                                                       "-EXECABORT Transaction discarded because of previous errors.\r\n"));
    shared.noreplicaserr = createObject(OBJ_STRING, sdsnew(
                                                        "-NOREPLICAS Not enough good replicas to write.\r\n"));
    shared.busykeyerr = createObject(OBJ_STRING, sdsnew(
                                                     "-BUSYKEY Target key name already exists.\r\n"));
    shared.space = createObject(OBJ_STRING, sdsnew(" "));
    shared.colon = createObject(OBJ_STRING, sdsnew(":"));
    shared.plus = createObject(OBJ_STRING, sdsnew("+"));

    /* The shared NULL depends on the protocol version. */
    shared.null[0] = NULL;
    shared.null[1] = NULL;
    shared.null[2] = createObject(OBJ_STRING, sdsnew("$-1\r\n"));
    shared.null[3] = createObject(OBJ_STRING, sdsnew("_\r\n"));

    shared.nullarray[0] = NULL;
    shared.nullarray[1] = NULL;
    shared.nullarray[2] = createObject(OBJ_STRING, sdsnew("*-1\r\n"));
    shared.nullarray[3] = createObject(OBJ_STRING, sdsnew("_\r\n"));

    shared.emptymap[0] = NULL;
    shared.emptymap[1] = NULL;
    shared.emptymap[2] = createObject(OBJ_STRING, sdsnew("*0\r\n"));
    shared.emptymap[3] = createObject(OBJ_STRING, sdsnew("%0\r\n"));

    shared.emptyset[0] = NULL;
    shared.emptyset[1] = NULL;
    shared.emptyset[2] = createObject(OBJ_STRING, sdsnew("*0\r\n"));
    shared.emptyset[3] = createObject(OBJ_STRING, sdsnew("~0\r\n"));

    for (j = 0; j < PROTO_SHARED_SELECT_CMDS; j++)
    {
        char dictid_str[64];
        int dictid_len;

        dictid_len = ll2string(dictid_str, sizeof(dictid_str), j);
        shared.select[j] = createObject(OBJ_STRING,
                                        sdscatprintf(sdsempty(),
                                                     "*2\r\n$6\r\nSELECT\r\n$%d\r\n%s\r\n",
                                                     dictid_len, dictid_str));
    }
    shared.messagebulk = createStringObject("$7\r\nmessage\r\n", 13);
    shared.pmessagebulk = createStringObject("$8\r\npmessage\r\n", 14);
    shared.subscribebulk = createStringObject("$9\r\nsubscribe\r\n", 15);
    shared.unsubscribebulk = createStringObject("$11\r\nunsubscribe\r\n", 18);
    shared.psubscribebulk = createStringObject("$10\r\npsubscribe\r\n", 17);
    shared.punsubscribebulk = createStringObject("$12\r\npunsubscribe\r\n", 19);
    shared.del = createStringObject("DEL", 3);
    shared.unlink = createStringObject("UNLINK", 6);
    shared.rpop = createStringObject("RPOP", 4);
    shared.lpop = createStringObject("LPOP", 4);
    shared.lpush = createStringObject("LPUSH", 5);
    shared.rpoplpush = createStringObject("RPOPLPUSH", 9);
    shared.zpopmin = createStringObject("ZPOPMIN", 7);
    shared.zpopmax = createStringObject("ZPOPMAX", 7);
    shared.multi = createStringObject("MULTI", 5);
    shared.exec = createStringObject("EXEC", 4);
    for (j = 0; j < OBJ_SHARED_INTEGERS; j++)
    {
        shared.integers[j] =
            makeObjectShared(createObject(OBJ_STRING, (void *)(long)j));
        shared.integers[j]->encoding = OBJ_ENCODING_INT;
    }
    for (j = 0; j < OBJ_SHARED_BULKHDR_LEN; j++)
    {
        shared.mbulkhdr[j] = createObject(OBJ_STRING,
                                          sdscatprintf(sdsempty(), "*%d\r\n", j));
        shared.bulkhdr[j] = createObject(OBJ_STRING,
                                         sdscatprintf(sdsempty(), "$%d\r\n", j));
    }
    /* The following two shared objects, minstring and maxstrings, are not
     * actually used for their value but as a special object meaning
     * respectively the minimum possible string and the maximum possible
     * string in string comparisons for the ZRANGEBYLEX command. */
    shared.minstring = sdsnew("minstring");
    shared.maxstring = sdsnew("maxstring");
}

// 为 `server` 数据结构设置默认值
void initServerConfig(void)
{
    int j;
    //更新unix时间的缓存值
    updateCachedTime(1);
    //获取一个随机Hex字符串作为服务器运行id
    getRandomHexChars(server.runid, CONFIG_RUN_ID_SIZE);
    server.runid[CONFIG_RUN_ID_SIZE] = '\0';
    //用一个新的随机id改变当前实例的复制id 这会阻止主节点和从节点之间的数据同步
    changeReplicationId();
    //清除（无效）辅助复制 ID。 例如，在完全重新同步之后，当我们开始新的复制时，就会发生这种情况
    clearReplicationId2();
    server.hz = CONFIG_DEFAULT_HZ;   /* Initialize it ASAP, even if it may get
                                        updated later after loading the config.
                                        This value may be used before the server
                                        is initialized. 尽快初始化它，即使它可能会在加载配置后更新。该值可以在服务器初始化完成之前使用。*/
    server.timezone = getTimeZone(); /* Initialized by tzset(). */
    server.configfile = NULL;
    server.executable = NULL;
    server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    server.bindaddr_count = 0;
    server.unixsocketperm = CONFIG_DEFAULT_UNIX_SOCKET_PERM;
    server.ipfd_count = 0;
    server.tlsfd_count = 0;
    server.sofd = -1;
    server.active_expire_enabled = 1;
    server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    server.saveparams = NULL;
    server.loading = 0;
    server.logfile = zstrdup(CONFIG_DEFAULT_LOGFILE);
    server.aof_state = AOF_OFF;
    server.aof_rewrite_base_size = 0;
    server.aof_rewrite_scheduled = 0;
    server.aof_flush_sleep = 0;
    server.aof_last_fsync = time(NULL);
    server.aof_rewrite_time_last = -1;
    server.aof_rewrite_time_start = -1;
    server.aof_lastbgrewrite_status = C_OK;
    server.aof_delayed_fsync = 0;
    server.aof_fd = -1;
    server.aof_selected_db = -1; /* Make sure the first time will not match */
    server.aof_flush_postponed_start = 0;
    server.pidfile = NULL;
    server.active_defrag_running = 0;
    server.notify_keyspace_events = 0;
    server.blocked_clients = 0;
    memset(server.blocked_clients_by_type, 0,
           sizeof(server.blocked_clients_by_type));
    server.shutdown_asap = 0;
    //字符串复制
    server.cluster_configfile = zstrdup(CONFIG_DEFAULT_CLUSTER_CONFIG_FILE);
    server.cluster_module_flags = CLUSTER_MODULE_FLAG_NONE;
    server.migrate_cached_sockets = dictCreate(&migrateCacheDictType, NULL);
    server.next_client_id = 1; /* Client IDs, start from 1 .*/
    server.loading_process_events_interval_bytes = (1024 * 1024 * 2);

    server.lruclock = getLRUClock();
    resetServerSaveParams();
    //服务器会在1分钟内有10000次改变或者5分钟内100次改变，或者1个小时内一次改变的时候进行追加保存
    appendServerSaveParams(60 * 60, 1); /* save after 1 hour and 1 change */
    appendServerSaveParams(300, 100);   /* save after 5 minutes and 100 changes */
    appendServerSaveParams(60, 10000);  /* save after 1 minute and 10000 changes */

    /* Replication related 主从复制的一些相关内容 */
    server.masterauth = NULL;
    server.masterhost = NULL;
    server.masterport = 6379;
    server.master = NULL;
    server.cached_master = NULL;
    server.master_initial_offset = -1;
    server.repl_state = REPL_STATE_NONE;
    server.repl_transfer_tmpfile = NULL;
    server.repl_transfer_fd = -1;
    server.repl_transfer_s = NULL;
    server.repl_syncio_timeout = CONFIG_REPL_SYNCIO_TIMEOUT;
    server.repl_down_since = 0; /* Never connected, repl is down since EVER. 自从上次断开后从未连接*/
    server.master_repl_offset = 0;

    /* Replication partial resync backlog 主从复制部分重新同步积压缓冲区配置*/
    server.repl_backlog = NULL;
    server.repl_backlog_histlen = 0;
    server.repl_backlog_idx = 0;
    server.repl_backlog_off = 0;
    server.repl_no_slaves_since = time(NULL);

    /* Client output buffer limits 客户端输出缓冲区限制 */
    /* {0, 0, 0}, normal
     * {1024*1024*256, 1024*1024*64, 60}, slave
     * {1024*1024*32, 1024*1024*8, 60}  pubsub
     */
    for (j = 0; j < CLIENT_TYPE_OBUF_COUNT; j++)
        server.client_obuf_limits[j] = clientBufferLimitsDefaults[j];

    /* Linux OOM Score config */
    //{ 0, 200, 800 }
    for (j = 0; j < CONFIG_OOM_COUNT; j++)
        server.oom_score_adj_values[j] = configOOMScoreAdjValuesDefaults[j];

    /* Double constants initialization 初始化双精度常量*/
    R_Zero = 0.0;
    R_PosInf = 1.0 / R_Zero;
    R_NegInf = -1.0 / R_Zero;
    R_Nan = R_Zero / R_Zero;

    /* Command table -- we initialize it here as it is part of the
     * initial configuration, since command names may be changed via
     * redis.conf using the rename-command directive.
     * 命令表——我们在这里对其进行初始化，因为它是初始配置的一部分，
     * 命令名称可以通过 redis.conf 使用 rename-command 指令进行更改。
     * */
    server.commands = dictCreate(&commandTableDictType, NULL);
    server.orig_commands = dictCreate(&commandTableDictType, NULL);
    populateCommandTable();
    server.delCommand = lookupCommandByCString("del");
    server.multiCommand = lookupCommandByCString("multi");
    server.lpushCommand = lookupCommandByCString("lpush");
    server.lpopCommand = lookupCommandByCString("lpop");
    server.rpopCommand = lookupCommandByCString("rpop");
    server.zpopminCommand = lookupCommandByCString("zpopmin");
    server.zpopmaxCommand = lookupCommandByCString("zpopmax");
    server.sremCommand = lookupCommandByCString("srem");
    server.execCommand = lookupCommandByCString("exec");
    server.expireCommand = lookupCommandByCString("expire");
    server.pexpireCommand = lookupCommandByCString("pexpire");
    server.xclaimCommand = lookupCommandByCString("xclaim");
    server.xgroupCommand = lookupCommandByCString("xgroup");
    server.rpoplpushCommand = lookupCommandByCString("rpoplpush");

    /* Debugging Debugging的一些配置*/
    server.assert_failed = "<no assertion failed>";
    server.assert_file = "<no file>";
    server.assert_line = 0;
    server.bug_report_start = 0;
    server.watchdog_period = 0;

    /* By default we want scripts to be always replicated by effects
     * (single commands executed by the script), and not by sending the
     * script to the slave / AOF. This is the new way starting from
     * Redis 5. However it is possible to revert it via redis.conf.
     * 默认情况下，我们希望脚本始终通过结果（脚本执行的单个命令）来复制，
     * 而不是通过将脚本发送到从节点/AOF。
     * 这是从 Redis 5 开始的新方式。但是可以通过 redis.conf 恢复它。
     * */
    server.lua_always_replicate_commands = 1;
    //初始化系统配置默认值，在没有给配置文件的情况下
    initConfigValues();
}

extern char **environ;

/* Restart the server, executing the same executable that started this
 * instance, with the same arguments and configuration file.
 *
 * The function is designed to directly call execve() so that the new
 * server instance will retain the PID of the previous one.
 *
 * The list of flags, that may be bitwise ORed together, alter the
 * behavior of this function:
 *
 * RESTART_SERVER_NONE              No flags.
 * RESTART_SERVER_GRACEFULLY        Do a proper shutdown before restarting.
 * RESTART_SERVER_CONFIG_REWRITE    Rewrite the config file before restarting.
 *
 * On success the function does not return, because the process turns into
 * a different process. On error C_ERR is returned. */
int restartServer(int flags, mstime_t delay)
{
    int j;

    /* Check if we still have accesses to the executable that started this
     * server instance. */
    if (access(server.executable, X_OK) == -1)
    {
        serverLog(LL_WARNING, "Can't restart: this process has no "
                              "permissions to execute %s",
                  server.executable);
        return C_ERR;
    }

    /* Config rewriting. */
    if (flags & RESTART_SERVER_CONFIG_REWRITE &&
        server.configfile &&
        rewriteConfig(server.configfile, 0) == -1)
    {
        serverLog(LL_WARNING, "Can't restart: configuration rewrite process "
                              "failed");
        return C_ERR;
    }

    /* Perform a proper shutdown. */
    if (flags & RESTART_SERVER_GRACEFULLY &&
        prepareForShutdown(SHUTDOWN_NOFLAGS) != C_OK)
    {
        serverLog(LL_WARNING, "Can't restart: error preparing for shutdown");
        return C_ERR;
    }

    /* Close all file descriptors, with the exception of stdin, stdout, strerr
     * which are useful if we restart a Redis server which is not daemonized. */
    for (j = 3; j < (int)server.maxclients + 1024; j++)
    {
        /* Test the descriptor validity before closing it, otherwise
         * Valgrind issues a warning on close(). */
        if (fcntl(j, F_GETFD) != -1)
            close(j);
    }

    /* Execute the server with the original command line. */
    if (delay)
        usleep(delay * 1000);
    zfree(server.exec_argv[0]);
    server.exec_argv[0] = zstrdup(server.executable);
    execve(server.executable, server.exec_argv, environ);

    /* If an error occurred here, there is nothing we can do, but exit. */
    _exit(1);

    return C_ERR; /* Never reached. */
}

static void readOOMScoreAdj(void)
{
#ifdef HAVE_PROC_OOM_SCORE_ADJ
    char buf[64];
    int fd = open("/proc/self/oom_score_adj", O_RDONLY);

    if (fd < 0)
        return;
    if (read(fd, buf, sizeof(buf)) > 0)
        server.oom_score_adj_base = atoi(buf);
    close(fd);
#endif
}

/* This function will configure the current process's oom_score_adj according
 * to user specified configuration. This is currently implemented on Linux
 * only.
 *
 * 此函数将根据用户指定的配置配置当前进程的oom_score_adj。这目前仅在Linux上实现。（取值-1000-1000）-1000代表永远不会被kill
 * A process_class value of -1 implies OOM_CONFIG_MASTER or OOM_CONFIG_REPLICA,
 * depending on current role.
 *
 * process_class值为-1表示OOM_CONFIG_MASTER或OOM_CONFIG_REPLICA，具体取决于当前角色。
 */
int setOOMScoreAdj(int process_class)
{

    if (server.oom_score_adj == OOM_SCORE_ADJ_NO)
        return C_OK;
    if (process_class == -1)
        process_class = (server.masterhost ? CONFIG_OOM_REPLICA : CONFIG_OOM_MASTER);

    serverAssert(process_class >= 0 && process_class < CONFIG_OOM_COUNT);

#ifdef HAVE_PROC_OOM_SCORE_ADJ
    int fd;
    int val;
    char buf[64];

    val = server.oom_score_adj_values[process_class];
    if (server.oom_score_adj == OOM_SCORE_RELATIVE)
        val += server.oom_score_adj_base;
    if (val > 1000)
        val = 1000;
    if (val < -1000)
        val = -1000;

    snprintf(buf, sizeof(buf) - 1, "%d\n", val);

    fd = open("/proc/self/oom_score_adj", O_WRONLY);
    if (fd < 0 || write(fd, buf, strlen(buf)) < 0)
    {
        serverLog(LOG_WARNING, "Unable to write oom_score_adj: %s", strerror(errno));
        if (fd != -1)
            close(fd);
        return C_ERR;
    }

    close(fd);
    return C_OK;
#else
    /* Unsupported */
    return C_ERR;
#endif
}

/* This function will try to raise the max number of open files accordingly to
 * the configured max number of clients. It also reserves a number of file
 * descriptors (CONFIG_MIN_RESERVED_FDS) for extra operations of
 * persistence, listening sockets, log files and so forth.
 * 此函数将尝试根据配置的最大客户端数来提高打开文件的最大数量。（理论上一个客户端最少对应一个文件描述符）
 * 它还保留了一些文件描述符（CONFIG_MIN_RESERVED_FDS），用于持久性、侦听socket、日志文件等的额外操作。
 * If it will not be possible to set the limit accordingly to the configured
 * max number of clients, the function will do the reverse setting
 * server.maxclients to the value that we can actually handle.
 * 如果无法根据配置的最大客户端数设置限制，
 * 则该函数会将 server.maxclients 反向设置为我们实际可以处理的值。*/
void adjustOpenFilesLimit(void)
{
    rlim_t maxfiles = server.maxclients + CONFIG_MIN_RESERVED_FDS;
    struct rlimit limit;
    //读取进程能打开的最大文件描述符 RLIMIT_NOFILE(一个进程能打开的最大文件数，内核默认是1024)
    if (getrlimit(RLIMIT_NOFILE, &limit) == -1)
    {
        serverLog(LL_WARNING, "Unable to obtain the current NOFILE limit (%s), assuming 1024 and setting the max clients configuration accordingly.", strerror(errno));
        server.maxclients = 1024 - CONFIG_MIN_RESERVED_FDS;
    }
    else
    {
        rlim_t oldlimit = limit.rlim_cur; //软限制，表示对资源没有限制

        /* Set the max number of files if the current limit is not enough
         * for our needs.
        如果当前的限制不足以应对我们的需求，我们设置最大文件数量 */
        if (oldlimit < maxfiles)
        {
            rlim_t bestlimit;
            int setrlimit_error = 0;

            /* Try to set the file limit to match 'maxfiles' or at least
             * to the higher value supported less than maxfiles.
             尝试将文件限制设置为 'maxfiles' 或至少设置为小于 maxfiles 但所支持的更高值。*/
            bestlimit = maxfiles;
            while (bestlimit > oldlimit)
            {
                //每一次bestlimit减少16个数量，达成目标退出，失败则再次减少16
                rlim_t decr_step = 16;

                limit.rlim_cur = bestlimit;
                limit.rlim_max = bestlimit;
                if (setrlimit(RLIMIT_NOFILE, &limit) != -1)
                    break;
                setrlimit_error = errno;

                /* We failed to set file limit to 'bestlimit'. Try with a
                 * smaller limit decrementing by a few FDs per iteration.
                 我们未能将文件限制设置为“bestlimit”。
                 尝试使用较小的限制，每次迭代减少几个 数量 */
                if (bestlimit < decr_step)
                {
                    //如果已经减少到16以下任然无法设置最大文件成功，我们也退出
                    break;
                }
                bestlimit -= decr_step;
            }

            /* Assume that the limit we get initially is still valid if
             * our last try was even lower.
             如果循环下来，bestlimit还不如一开始获取的 limit.rlim_cur，那么我们暂时将bestlimit设置成oldlimit */
            if (bestlimit < oldlimit)
            {
                bestlimit = oldlimit;
            }
            if (bestlimit < maxfiles)
            {
                //如果最终的设置和我们想要的还是差了点
                unsigned int old_maxclients = server.maxclients;
                server.maxclients = bestlimit - CONFIG_MIN_RESERVED_FDS;
                /* maxclients is unsigned so may overflow: in order
                 * to check if maxclients is now logically less than 1
                 * we test indirectly via bestlimit.
                 * maxclients 是无符号的，因此可能会溢出：
                 * 为了检查 maxclients 现在是否在逻辑上小于 1，我们通过 bestlimit 间接测试。*/
                if (bestlimit <= CONFIG_MIN_RESERVED_FDS)
                {
                    //如果配置的值小于最小预设值，退出程序（server.maxclients <= 0  或者 系统文件描述符支持小于32）
                    serverLog(LL_WARNING, "Your current 'ulimit -n' "
                                          "of %llu is not enough for the server to start. "
                                          "Please increase your open file limit to at least "
                                          "%llu. Exiting.",
                              (unsigned long long)oldlimit,
                              (unsigned long long)maxfiles);
                    exit(1);
                }
                serverLog(LL_WARNING, "You requested maxclients of %d "
                                      "requiring at least %llu max file descriptors.",
                          old_maxclients,
                          (unsigned long long)maxfiles);
                serverLog(LL_WARNING, "Server can't set maximum open files "
                                      "to %llu because of OS error: %s.",
                          (unsigned long long)maxfiles, strerror(setrlimit_error));
                serverLog(LL_WARNING, "Current maximum open files is %llu. "
                                      "maxclients has been reduced to %d to compensate for "
                                      "low ulimit. "
                                      "If you need higher maxclients increase 'ulimit -n'.",
                          (unsigned long long)bestlimit, server.maxclients);
            }
            else
            {
                //如果最终的设置和我们想要的相符
                serverLog(LL_NOTICE, "Increased maximum number of open files "
                                     "to %llu (it was originally set to %llu).",
                          (unsigned long long)maxfiles,
                          (unsigned long long)oldlimit);
            }
        }
    }
}

/* Check that server.tcp_backlog can be actually enforced in Linux according
 * to the value of /proc/sys/net/core/somaxconn, or warn about it.
 根据 /proc/sys/net/core/somaxconn 的值，检查 server.tcp_backlog 是否可以在 Linux 中实际执行，或者警告它。*/
void checkTcpBacklogSettings(void)
{
#ifdef HAVE_PROC_SOMAXCONN
    FILE *fp = fopen("/proc/sys/net/core/somaxconn", "r");
    char buf[1024];
    if (!fp)
        return;
    if (fgets(buf, sizeof(buf), fp) != NULL)
    {
        int somaxconn = atoi(buf);
        if (somaxconn > 0 && somaxconn < server.tcp_backlog)
        {
            serverLog(LL_WARNING, "WARNING: The TCP backlog setting of %d cannot be enforced because /proc/sys/net/core/somaxconn is set to the lower value of %d.", server.tcp_backlog, somaxconn);
        }
    }
    fclose(fp);
#endif
}

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
            printf("bind Tcp6 * fd:%d\n", fds[*count]);
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
                printf("bind Tcp4 * fd:%d\n", fds[*count]);
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

/* Resets the stats that we expose via INFO or other means that we want
 * to reset via CONFIG RESETSTAT. The function is also used in order to
 * initialize these fields in initServer() at server startup.
 * 重置服务器统计信息，一般用在服务器启动的时候，或者当收到config resetstat命令的时候
 * */
void resetServerStats(void)
{
    int j;

    server.stat_numcommands = 0;
    server.stat_numconnections = 0;
    server.stat_expiredkeys = 0;
    server.stat_expired_stale_perc = 0;
    server.stat_expired_time_cap_reached_count = 0;
    server.stat_expire_cycle_time_used = 0;
    server.stat_evictedkeys = 0;
    server.stat_keyspace_misses = 0;
    server.stat_keyspace_hits = 0;
    server.stat_active_defrag_hits = 0;
    server.stat_active_defrag_misses = 0;
    server.stat_active_defrag_key_hits = 0;
    server.stat_active_defrag_key_misses = 0;
    server.stat_active_defrag_scanned = 0;
    server.stat_fork_time = 0;
    server.stat_fork_rate = 0;
    server.stat_rejected_conn = 0;
    server.stat_sync_full = 0;
    server.stat_sync_partial_ok = 0;
    server.stat_sync_partial_err = 0;
    server.stat_io_reads_processed = 0;
    server.stat_total_reads_processed = 0;
    server.stat_io_writes_processed = 0;
    server.stat_total_writes_processed = 0;
    for (j = 0; j < STATS_METRIC_COUNT; j++)
    {
        server.inst_metric[j].idx = 0;
        server.inst_metric[j].last_sample_time = mstime();
        server.inst_metric[j].last_sample_count = 0;
        memset(server.inst_metric[j].samples, 0,
               sizeof(server.inst_metric[j].samples));
    }
    server.stat_net_input_bytes = 0;
    server.stat_net_output_bytes = 0;
    server.stat_unexpected_error_replies = 0;
    server.aof_delayed_fsync = 0;
}

/* Make the thread killable at any time, so that kill threads functions
 * can work reliably (default cancelability type is PTHREAD_CANCEL_DEFERRED).
 * Needed for pthread_cancel used by the fast memory test used by the crash report. */
void makeThreadKillable(void)
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
}

/**
 * @brief 初始化服务器 创建共享对象 创建事件循环 监听端口
 *
 */
void initServer(void)
{
    int j;
    /**
     * 处理信号量
     */
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSignalHandlers();
    makeThreadKillable();
    /**
     * syslog 是 linux 系统自带的，主要是为了 daemon 进程提供日志服务，
     * 我们在前面讲过。deamon 进程无法打印到终端，那么如何方便的接受输出的日志呢？
     * linux 提供了 syslog 服务，该服务支持打印各种级别的日志以及输出位置（本地或者远程均可）
     */
    if (server.syslog_enabled)
    {
        openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT,
                server.syslog_facility);
    }

    /* Initialization after setting defaults from the config system.
    在设置完系统默认配置之后初始化 */
    server.aof_state = server.aof_enabled ? AOF_ON : AOF_OFF;
    server.hz = server.config_hz;
    server.pid = getpid();
    server.in_fork_child = CHILD_TYPE_NONE;
    server.main_thread_id = pthread_self();
    server.current_client = NULL;
    server.fixed_time_expire = 0;
    server.clients = listCreate();
    server.clients_index = raxNew();
    server.clients_to_close = listCreate();
    server.slaves = listCreate();
    server.monitors = listCreate();
    server.clients_pending_write = listCreate();
    server.clients_pending_read = listCreate();
    server.clients_timeout_table = raxNew();
    server.slaveseldb = -1; /* Force to emit the first SELECT command. */
    server.unblocked_clients = listCreate();
    server.ready_keys = listCreate();
    server.clients_waiting_acks = listCreate();
    server.get_ack_from_slaves = 0;
    server.clients_paused = 0;
    server.events_processed_while_blocked = 0;
    server.system_memory_size = zmalloc_get_memory_size();

    if ((server.tls_port || server.tls_replication || server.tls_cluster) && tlsConfigure(&server.tls_ctx_config) == C_ERR)
    {
        //如果配置tls的一些设置却没有给配置文件，退出程序
        serverLog(LL_WARNING, "Failed to configure TLS. Check logs for more info.");
        exit(1);
    }
    /**
     * 创建一些共享对象，例如错误信息，发生通用错误的时候不需要重新分配内存再释放内存。
     */
    createSharedObjects();
    //根据最大客户端数量配置可打开的最大文件描述符
    adjustOpenFilesLimit();
    server.el = aeCreateEventLoop(server.maxclients + CONFIG_FDSET_INCR);
    if (server.el == NULL)
    {
        //创建事件循环失败
        serverLog(LL_WARNING,
                  "Failed creating the event loop. Error message: '%s'",
                  strerror(errno));
        exit(1);
    }
    server.db = zmalloc(sizeof(redisDb) * server.dbnum);

    /* Open the TCP listening socket for the user commands. 为用户命令打开TCP监听socket */
    if (server.port != 0 &&
        listenToPort(server.port, server.ipfd, &server.ipfd_count) == C_ERR)
        exit(1);
    if (server.tls_port != 0 &&
        listenToPort(server.tls_port, server.tlsfd, &server.tlsfd_count) == C_ERR)
        exit(1);
    /**
     * @brief unix domain socket 是在socket架构上发展起来的用于同一台主机的进程间通讯（IPC: Inter-Process Communication），
     * 它不需要经过网络协议栈，不需要打包拆包、计算校验和、维护序号和应答等，
     * 只是将应用层数据从一个进程拷贝到另一个进程。
     * UNIX Domain Socket有SOCK_DGRAM或SOCK_STREAM两种工作模式，类似于UDP和TCP，
     * 但是面向消息的UNIX Domain Socket也是可靠的，消息既不会丢失也不会顺序错乱。
     * UNIX Domain Socket可用于两个没有亲缘关系的进程，是全双工的，是目前使用最广泛的IPC机制，
     * 比如X Window服务器和GUI程序之间就是通过UNIX Domain Socket通讯的。
     */
    /* Open the listening Unix domain socket.打开在监听的Unix domain socket 默认是NULL*/
    if (server.unixsocket != NULL)
    {
        unlink(server.unixsocket); /* don't care if this fails 如果失败了也没关系*/
        server.sofd = anetUnixServer(server.neterr, server.unixsocket,
                                     server.unixsocketperm, server.tcp_backlog);
        if (server.sofd == ANET_ERR)
        {
            serverLog(LL_WARNING, "Opening Unix socket: %s", server.neterr);
            exit(1);
        }
        anetNonBlock(NULL, server.sofd);
    }

    /* Abort if there are no listening sockets at all.如果根本没有侦听套接字，则中止。*/
    if (server.ipfd_count == 0 && server.tlsfd_count == 0 && server.sofd < 0)
    {
        serverLog(LL_WARNING, "Configured to not listen anywhere, exiting.");
        exit(1);
    }

    /* Create the Redis databases, and initialize other internal state. 创建Redis数据库，或者初始化其他内部状态 databases默认是16个数据库*/
    for (j = 0; j < server.dbnum; j++)
    {
        server.db[j].dict = dictCreate(&dbDictType, NULL);
        server.db[j].expires = dictCreate(&keyptrDictType, NULL);
        server.db[j].expires_cursor = 0;
        server.db[j].blocking_keys = dictCreate(&keylistDictType, NULL);
        server.db[j].ready_keys = dictCreate(&objectKeyPointerValueDictType, NULL);
        server.db[j].watched_keys = dictCreate(&keylistDictType, NULL);
        server.db[j].id = j;
        server.db[j].avg_ttl = 0;
        server.db[j].defrag_later = listCreate();
        listSetFreeMethod(server.db[j].defrag_later, (void (*)(void *))sdsfree);
    }
    //（1）redis采取的是LRU近似算法，也就是对keys进行采样，然后在采样结果中进行数据清理
    //（2）redis 3.0开始，在LRU近似算法中引入了pool机制，表现可以跟真正的LRU算法相当，但是还是有所差距的，不过这样可以减少内存的消耗
    //（3）redis LRU算法，是采样之后再做LRU清理的，跟真正的、传统、全量的LRU算法是不太一样的
    //（4）maxmemory-samples，比如5，可以设置采样的大小，如果设置为10，那么效果会更好，不过也会耗费更多的CPU资源
    evictionPoolAlloc(); /* Initialize the LRU keys pool. 初始化LRU keys 池，LRU（最近最少使用算法）算法使用*/
    server.pubsub_channels = dictCreate(&keylistDictType, NULL);
    server.pubsub_patterns = listCreate();
    server.pubsub_patterns_dict = dictCreate(&keylistDictType, NULL);
    listSetFreeMethod(server.pubsub_patterns, freePubsubPattern);
    listSetMatchMethod(server.pubsub_patterns, listMatchPubsubPattern);
    server.cronloops = 0;
    server.rdb_child_pid = -1;
    server.aof_child_pid = -1;
    server.module_child_pid = -1;
    server.rdb_child_type = RDB_CHILD_TYPE_NONE;
    server.rdb_pipe_conns = NULL;
    server.rdb_pipe_numconns = 0;
    server.rdb_pipe_numconns_writing = 0;
    server.rdb_pipe_buff = NULL;
    server.rdb_pipe_bufflen = 0;
    server.rdb_bgsave_scheduled = 0;
    server.child_info_pipe[0] = -1;
    server.child_info_pipe[1] = -1;
    server.child_info_data.magic = 0;
    /**
     * 如果需要，此函数将释放旧的AOF重写缓冲区，并初始化一个新的AOF重写缓冲区。
     * 它测试“server.aof_rewrite_buf_blocks”是否等于NULL，因此也可以用于第一次初始化。
     */
    aofRewriteBufferReset();
    server.aof_buf = sdsempty();
    server.lastsave = time(NULL); /* At startup we consider the DB saved. */
    server.lastbgsave_try = 0;    /* At startup we never tried to BGSAVE. */
    server.rdb_save_time_last = -1;
    server.rdb_save_time_start = -1;
    server.dirty = 0;
    //重置服务器统计信息，一般用在服务器启动的时候，或者当收到config resetstat命令的时候
    resetServerStats();
    /* A few stats we don't want to reset: server startup time, and peak mem. */
    server.stat_starttime = time(NULL);
    server.stat_peak_memory = 0;
    server.stat_rdb_cow_bytes = 0;
    server.stat_aof_cow_bytes = 0;
    server.stat_module_cow_bytes = 0;
    for (int j = 0; j < CLIENT_TYPE_COUNT; j++)
        server.stat_clients_type_memory[j] = 0;
    server.cron_malloc_stats.zmalloc_used = 0;
    server.cron_malloc_stats.process_rss = 0;
    server.cron_malloc_stats.allocator_allocated = 0;
    server.cron_malloc_stats.allocator_active = 0;
    server.cron_malloc_stats.allocator_resident = 0;
    server.lastbgsave_status = C_OK;
    server.aof_last_write_status = C_OK;
    server.aof_last_write_errno = 0;
    server.repl_good_slaves_count = 0;

    /* Create the timer callback, this is our way to process many background
     * operations incrementally, like clients timeout, eviction of unaccessed
     * expired keys and so forth.
     * 创建计时器回调，这是我们逐渐处理许多后台操作的方式，例如客户端超时、未访问的过期键的驱逐等。 */
    if (aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR)
    {
        serverPanic("Can't create event loop timers.");
        exit(1);
    }

    /* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets.
     * 创建一个事件处理程序以接受 TCP 和 Unix 域套接字中的新连接
     * 每绑定一个地址就创建一个事件处理器
     */
    for (j = 0; j < server.ipfd_count; j++)
    {
        printf("aeCreateFileEvent:ipfd:%d\n", server.ipfd[j]);
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE, acceptTcpHandler, NULL) == AE_ERR)
        {
            serverPanic(
                "Unrecoverable error creating server.ipfd file event.");
        }
    }
    for (j = 0; j < server.tlsfd_count; j++)
    {
        if (aeCreateFileEvent(server.el, server.tlsfd[j], AE_READABLE, acceptTLSHandler, NULL) == AE_ERR)
        {
            serverPanic(
                "Unrecoverable error creating server.tlsfd file event.");
        }
    }
    if (server.sofd > 0 && aeCreateFileEvent(server.el, server.sofd, AE_READABLE, acceptUnixHandler, NULL) == AE_ERR)
    {
        serverPanic("Unrecoverable error creating server.sofd file event.");
    }
    /* Register a readable event for the pipe used to awake the event loop
     * when a blocked client in a module needs attention.
     * 为管道去注册一个可读事件去唤醒事件循环，当模块中被阻塞的客户端需要处理时
     */
    if (aeCreateFileEvent(server.el, server.module_blocked_pipe[0], AE_READABLE, moduleBlockedClientPipeReadable, NULL) == AE_ERR)
    {
        serverPanic(
            "Error registering the readable event for the module "
            "blocked clients subsystem.");
    }

    /* Register before and after sleep handlers (note this needs to be done
     * before loading persistence since it is used by processEventsWhileBlocked.
     * 为sleep注册一个前置和后置处理器
     * 这需要在加载持久性之前完成，因为它由 processEventsWhileBlocked 使用。
     */
    aeSetBeforeSleepProc(server.el, beforeSleep);
    aeSetAfterSleepProc(server.el, afterSleep);

    /* Open the AOF file if needed. 如果需要，打开AOF文件*/
    if (server.aof_state == AOF_ON)
    {
        server.aof_fd = open(server.aof_filename,
                             O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (server.aof_fd == -1)
        {
            serverLog(LL_WARNING, "Can't open the append-only file: %s",
                      strerror(errno));
            exit(1);
        }
    }

    /* 32 bit instances are limited to 4GB of address space, so if there is
     * no explicit limit in the user provided configuration we set a limit
     * at 3 GB using maxmemory with 'noeviction' policy'. This avoids
     * useless crashes of the Redis instance for out of memory.
     *
     * 32 位实例被限制为 4GB 的地址空间，因此如果用户提供的配置中没有明确的限制，
     * 我们使用带有“noeviction”策略的 maxmemory 将限制设置为 3GB。
     * 这避免了 Redis 实例因内存不足而导致的无用崩溃。
     *  */
    if (server.arch_bits == 32 && server.maxmemory == 0)
    {
        serverLog(LL_WARNING, "Warning: 32 bit instance detected but no memory limit set. Setting 3 GB maxmemory limit with 'noeviction' policy now.");
        server.maxmemory = 3072LL * (1024 * 1024); /* 3 GB */
        server.maxmemory_policy = MAXMEMORY_NO_EVICTION;
    }
    //是否开启集群
    if (server.cluster_enabled)
        //初始化集群
        clusterInit();
    //初始化脚本缓存，仅在启动时调用。
    replicationScriptCacheInit();
    //初始化脚本环境
    scriptingInit(1);
    //初始化慢日志。 此函数应在服务器启动时调用一次
    slowlogInit();
    //初始化Latency Monitoring：可以帮助我们检查和排查引起延迟的原因。
    latencyMonitorInit();
}

/** Some steps in server initialization need to be done last (after modules
 * are loaded).
 * 有一些步骤需要在初始化之后在进行完成（在模块加载之后）
 * Specifically, creation of threads due to a race bug in ld.so, in which
 * Thread Local Storage initialization collides with dlopen call.
 * 具体来说，由于 ld.so 中线程本地存储初始化与 dlopen 调用发生冲突会导致创建线程出现Bug。
 * see: https://sourceware.org/bugzilla/show_bug.cgi?id=19329 */
void InitServerLast()
{
    //初始阻塞线程
    bioInit();
    //初始化线程 I/O 所需的数据结构。
    initThreadedIO();
    set_jemalloc_bg_thread(server.jemalloc_bg_thread);
    server.initial_memory_usage = zmalloc_used_memory();
}

/* Parse the flags string description 'strflags' and set them to the
 * command 'c'. If the flags are all valid C_OK is returned, otherwise
 * C_ERR is returned (yet the recognized flags are set in the command). */
int populateCommandTableParseFlags(struct redisCommand *c, char *strflags)
{
    int argc;
    sds *argv;

    /* Split the line into arguments for processing. */
    argv = sdssplitargs(strflags, &argc);
    if (argv == NULL)
        return C_ERR;

    for (int j = 0; j < argc; j++)
    {
        char *flag = argv[j];
        if (!strcasecmp(flag, "write"))
        {
            c->flags |= CMD_WRITE | CMD_CATEGORY_WRITE;
        }
        else if (!strcasecmp(flag, "read-only"))
        {
            c->flags |= CMD_READONLY | CMD_CATEGORY_READ;
        }
        else if (!strcasecmp(flag, "use-memory"))
        {
            c->flags |= CMD_DENYOOM;
        }
        else if (!strcasecmp(flag, "admin"))
        {
            c->flags |= CMD_ADMIN | CMD_CATEGORY_ADMIN | CMD_CATEGORY_DANGEROUS;
        }
        else if (!strcasecmp(flag, "pub-sub"))
        {
            c->flags |= CMD_PUBSUB | CMD_CATEGORY_PUBSUB;
        }
        else if (!strcasecmp(flag, "no-script"))
        {
            c->flags |= CMD_NOSCRIPT;
        }
        else if (!strcasecmp(flag, "random"))
        {
            c->flags |= CMD_RANDOM;
        }
        else if (!strcasecmp(flag, "to-sort"))
        {
            c->flags |= CMD_SORT_FOR_SCRIPT;
        }
        else if (!strcasecmp(flag, "ok-loading"))
        {
            c->flags |= CMD_LOADING;
        }
        else if (!strcasecmp(flag, "ok-stale"))
        {
            c->flags |= CMD_STALE;
        }
        else if (!strcasecmp(flag, "no-monitor"))
        {
            c->flags |= CMD_SKIP_MONITOR;
        }
        else if (!strcasecmp(flag, "no-slowlog"))
        {
            c->flags |= CMD_SKIP_SLOWLOG;
        }
        else if (!strcasecmp(flag, "cluster-asking"))
        {
            c->flags |= CMD_ASKING;
        }
        else if (!strcasecmp(flag, "fast"))
        {
            c->flags |= CMD_FAST | CMD_CATEGORY_FAST;
        }
        else if (!strcasecmp(flag, "no-auth"))
        {
            c->flags |= CMD_NO_AUTH;
        }
        else
        {
            /* Parse ACL categories here if the flag name starts with @. */
            uint64_t catflag;
            if (flag[0] == '@' &&
                (catflag = ACLGetCommandCategoryFlagByName(flag + 1)) != 0)
            {
                c->flags |= catflag;
            }
            else
            {
                sdsfreesplitres(argv, argc);
                return C_ERR;
            }
        }
    }
    /* If it's not @fast is @slow in this binary world. */
    if (!(c->flags & CMD_CATEGORY_FAST))
        c->flags |= CMD_CATEGORY_SLOW;

    sdsfreesplitres(argv, argc);
    return C_OK;
}

/* Populates the Redis Command Table starting from the hard coded list
 * we have on top of server.c file.
   从我们在 server.c 文件顶部的硬编码列表开始填充 Redis 命令表。*/
void populateCommandTable(void)
{
    int j;
    int numcommands = sizeof(redisCommandTable) / sizeof(struct redisCommand);

    for (j = 0; j < numcommands; j++)
    {
        struct redisCommand *c = redisCommandTable + j;
        int retval1, retval2;

        /* Translate the command string flags description into an actual
         * set of flags. */
        if (populateCommandTableParseFlags(c, c->sflags) == C_ERR)
            serverPanic("Unsupported command flag");

        c->id = ACLGetCommandID(c->name); /* Assign the ID used for ACL. */
        retval1 = dictAdd(server.commands, sdsnew(c->name), c);
        /* Populate an additional dictionary that will be unaffected
         * by rename-command statements in redis.conf. */
        retval2 = dictAdd(server.orig_commands, sdsnew(c->name), c);
        serverAssert(retval1 == DICT_OK && retval2 == DICT_OK);
    }
}

void resetCommandTableStats(void)
{
    struct redisCommand *c;
    dictEntry *de;
    dictIterator *di;

    di = dictGetSafeIterator(server.commands);
    while ((de = dictNext(di)) != NULL)
    {
        c = (struct redisCommand *)dictGetVal(de);
        c->microseconds = 0;
        c->calls = 0;
    }
    dictReleaseIterator(di);
}

/* ========================== Redis OP Array API ============================ */

void redisOpArrayInit(redisOpArray *oa)
{
    oa->ops = NULL;
    oa->numops = 0;
}

int redisOpArrayAppend(redisOpArray *oa, struct redisCommand *cmd, int dbid,
                       robj **argv, int argc, int target)
{
    redisOp *op;

    oa->ops = zrealloc(oa->ops, sizeof(redisOp) * (oa->numops + 1));
    op = oa->ops + oa->numops;
    op->cmd = cmd;
    op->dbid = dbid;
    op->argv = argv;
    op->argc = argc;
    op->target = target;
    oa->numops++;
    return oa->numops;
}

void redisOpArrayFree(redisOpArray *oa)
{
    while (oa->numops)
    {
        int j;
        redisOp *op;

        oa->numops--;
        op = oa->ops + oa->numops;
        for (j = 0; j < op->argc; j++)
            decrRefCount(op->argv[j]);
        zfree(op->argv);
    }
    zfree(oa->ops);
}

/* ====================== Commands lookup and execution ===================== */

struct redisCommand *lookupCommand(sds name)
{
    return dictFetchValue(server.commands, name);
}

struct redisCommand *lookupCommandByCString(const char *s)
{
    struct redisCommand *cmd;
    sds name = sdsnew(s);

    cmd = dictFetchValue(server.commands, name);
    sdsfree(name);
    return cmd;
}

/* Lookup the command in the current table, if not found also check in
 * the original table containing the original command names unaffected by
 * redis.conf rename-command statement.
 *
 * This is used by functions rewriting the argument vector such as
 * rewriteClientCommandVector() in order to set client->cmd pointer
 * correctly even if the command was renamed. */
struct redisCommand *lookupCommandOrOriginal(sds name)
{
    struct redisCommand *cmd = dictFetchValue(server.commands, name);

    if (!cmd)
        cmd = dictFetchValue(server.orig_commands, name);
    return cmd;
}

/* Propagate the specified command (in the context of the specified database id)
 * to AOF and Slaves.
 *
 * flags are an xor between:
 * + PROPAGATE_NONE (no propagation of command at all)
 * + PROPAGATE_AOF (propagate into the AOF file if is enabled)
 * + PROPAGATE_REPL (propagate into the replication link)
 *
 * This should not be used inside commands implementation since it will not
 * wrap the resulting commands in MULTI/EXEC. Use instead alsoPropagate(),
 * preventCommandPropagation(), forceCommandPropagation().
 *
 * However for functions that need to (also) propagate out of the context of a
 * command execution, for example when serving a blocked client, you
 * want to use propagate().
 */
void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc,
               int flags)
{
    if (server.aof_state != AOF_OFF && flags & PROPAGATE_AOF)
        feedAppendOnlyFile(cmd, dbid, argv, argc);
    if (flags & PROPAGATE_REPL)
        replicationFeedSlaves(server.slaves, dbid, argv, argc);
}

/* Used inside commands to schedule the propagation of additional commands
 * after the current command is propagated to AOF / Replication.
 *
 * 'cmd' must be a pointer to the Redis command to replicate, dbid is the
 * database ID the command should be propagated into.
 * Arguments of the command to propagate are passed as an array of redis
 * objects pointers of len 'argc', using the 'argv' vector.
 *
 * The function does not take a reference to the passed 'argv' vector,
 * so it is up to the caller to release the passed argv (but it is usually
 * stack allocated).  The function automatically increments ref count of
 * passed objects, so the caller does not need to. */
void alsoPropagate(struct redisCommand *cmd, int dbid, robj **argv, int argc,
                   int target)
{
    robj **argvcopy;
    int j;

    if (server.loading)
        return; /* No propagation during loading. */

    argvcopy = zmalloc(sizeof(robj *) * argc);
    for (j = 0; j < argc; j++)
    {
        argvcopy[j] = argv[j];
        incrRefCount(argv[j]);
    }
    redisOpArrayAppend(&server.also_propagate, cmd, dbid, argvcopy, argc, target);
}

/* It is possible to call the function forceCommandPropagation() inside a
 * Redis command implementation in order to to force the propagation of a
 * specific command execution into AOF / Replication. */
void forceCommandPropagation(client *c, int flags)
{
    if (flags & PROPAGATE_REPL)
        c->flags |= CLIENT_FORCE_REPL;
    if (flags & PROPAGATE_AOF)
        c->flags |= CLIENT_FORCE_AOF;
}

/* Avoid that the executed command is propagated at all. This way we
 * are free to just propagate what we want using the alsoPropagate()
 * API. */
void preventCommandPropagation(client *c)
{
    c->flags |= CLIENT_PREVENT_PROP;
}

/* AOF specific version of preventCommandPropagation(). */
void preventCommandAOF(client *c)
{
    c->flags |= CLIENT_PREVENT_AOF_PROP;
}

/* Replication specific version of preventCommandPropagation(). */
void preventCommandReplication(client *c)
{
    c->flags |= CLIENT_PREVENT_REPL_PROP;
}

/* Call() is the core of Redis execution of a command.
 *
 * Call函数是Redis执行命令的核心
 *
 * The following flags can be passed:
 * 下面的标识都可以备传入
 * CMD_CALL_NONE        No flags. 无标识
 * CMD_CALL_SLOWLOG     Check command speed and log in the slow log if needed. 如果需要，检查命令执行时间和记录慢日志
 * CMD_CALL_STATS       Populate command stats. 填充命令统计信息
 * CMD_CALL_PROPAGATE_AOF   Append command to AOF if it modified the dataset
 *                          or if the client flags are forcing propagation.
 * 如果 AOF 修改了数据集或客户端标志强制传播，则将命令附加到 AOF。
 *
 * CMD_CALL_PROPAGATE_REPL  Send command to slaves if it modified the dataset
 *                          or if the client flags are forcing propagation.
 * 如果它修改了数据集或者客户端标志强制传播，则向从节点发送命令。
 *
 * CMD_CALL_PROPAGATE   Alias for PROPAGATE_AOF|PROPAGATE_REPL.# PROPAGATE_AOF|PROPAGATE_REPL 的别名。
 * CMD_CALL_FULL        Alias for SLOWLOG|STATS|PROPAGATE. # SLOWLOG|STATS|PROPAGATE的别名
 *
 * The exact propagation behavior depends on the client flags.
 * Specifically:
 * 额外的传播行为依赖于客户端标志，例如：
 *
 * 1. If the client flags CLIENT_FORCE_AOF or CLIENT_FORCE_REPL are set
 *    and assuming the corresponding CMD_CALL_PROPAGATE_AOF/REPL is set
 *    in the call flags, then the command is propagated even if the
 *    dataset was not affected by the command.
 *
 * 1. 如果设置了客户端标志 CLIENT_FORCE_AOF 或 CLIENT_FORCE_REPL
 * 并假设在调用标志中设置了相应的 CMD_CALL_PROPAGATE_AOF/REPL，
 * 那么即使数据集不受命令影响，也会传播命令。
 *
 * 2. If the client flags CLIENT_PREVENT_REPL_PROP or CLIENT_PREVENT_AOF_PROP
 *    are set, the propagation into AOF or to slaves is not performed even
 *    if the command modified the dataset.
 *2. 如果设置了客户端标志 CLIENT_PREVENT_REPL_PROP 或 CLIENT_PREVENT_AOF_PROP，
 * 即使命令修改了数据集，也不会执行到 AOF 或从节点的传播。
 *
 * Note that regardless of the client flags, if CMD_CALL_PROPAGATE_AOF
 * or CMD_CALL_PROPAGATE_REPL are not set, then respectively AOF or
 * slaves propagation will never occur.
 *
 * 请注意，无论客户端标志如何，如果 CMD_CALL_PROPAGATE_AOF
 * 或 CMD_CALL_PROPAGATE_REPL 未设置，则分别不会发生 AOF  或从节点传播。
 *
 * Client flags are modified by the implementation of a given command
 * using the following API:
 *
 * 客户端的标识可以通过下面给到的api实现进行修改
 *
 * forceCommandPropagation(client *c, int flags);
 * preventCommandPropagation(client *c);
 * preventCommandAOF(client *c);
 * preventCommandReplication(client *c);
 *
 */
void call(client *c, int flags)
{
    long long dirty;
    ustime_t start, duration;
    int client_old_flags = c->flags;
    struct redisCommand *real_cmd = c->cmd;

    /* Send the command to clients in MONITOR mode if applicable.
     * Administrative commands are considered too dangerous to be shown.
     * 如果适用，以 MONITOR 模式将命令发送给客户端。管理命令被认为太危险而无法显示。
     * */
    if (listLength(server.monitors) && !server.loading && !(c->cmd->flags & (CMD_SKIP_MONITOR | CMD_ADMIN)))
    {
        replicationFeedMonitors(c, server.monitors, c->db->id, c->argv, c->argc);
    }

    /* Initialization: clear the flags that must be set by the command on
     * demand, and initialize the array for additional commands propagation. 
     * 初始化：清除必须由命令按需设置的标志，并初始化数组以进行其他命令传播。
     */
    c->flags &= ~(CLIENT_FORCE_AOF | CLIENT_FORCE_REPL | CLIENT_PREVENT_PROP);
    // 额外的命令传播
    redisOpArray prev_also_propagate = server.also_propagate;
    // 初始化额外的命令传播
    redisOpArrayInit(&server.also_propagate);

    /* Call the command. 执行命令*/
    //获取上次保存前所有数据变动的长度
    dirty = server.dirty;

    /* Update cache time, in case we have nested calls we want to
     * update only on the first call
     更新缓存时间，如果我们有嵌套调用，我们只想在第一次调用时更新 */
    if (server.fixed_time_expire++ == 0)
    {
        updateCachedTime(0);
    }

    start = server.ustime;
    //执行命令
    c->cmd->proc(c);
    duration = ustime() - start;
    dirty = server.dirty - dirty;
    if (dirty < 0)
        dirty = 0;

    /* After executing command, we will close the client after writing entire
     * reply if it is set 'CLIENT_CLOSE_AFTER_COMMAND' flag. */
    if (c->flags & CLIENT_CLOSE_AFTER_COMMAND)
    {
        c->flags &= ~CLIENT_CLOSE_AFTER_COMMAND;
        c->flags |= CLIENT_CLOSE_AFTER_REPLY;
    }

    /* When EVAL is called loading the AOF we don't want commands called
     * from Lua to go into the slowlog or to populate statistics. */
    if (server.loading && c->flags & CLIENT_LUA)
        flags &= ~(CMD_CALL_SLOWLOG | CMD_CALL_STATS);

    /* If the caller is Lua, we want to force the EVAL caller to propagate
     * the script if the command flag or client flag are forcing the
     * propagation. */
    if (c->flags & CLIENT_LUA && server.lua_caller)
    {
        if (c->flags & CLIENT_FORCE_REPL)
            server.lua_caller->flags |= CLIENT_FORCE_REPL;
        if (c->flags & CLIENT_FORCE_AOF)
            server.lua_caller->flags |= CLIENT_FORCE_AOF;
    }

    /* Log the command into the Slow log if needed, and populate the
     * per-command statistics that we show in INFO commandstats. */
    if (flags & CMD_CALL_SLOWLOG && !(c->cmd->flags & CMD_SKIP_SLOWLOG))
    {
        char *latency_event = (c->cmd->flags & CMD_FAST) ? "fast-command" : "command";
        latencyAddSampleIfNeeded(latency_event, duration / 1000);
        slowlogPushEntryIfNeeded(c, c->argv, c->argc, duration);
    }

    if (flags & CMD_CALL_STATS)
    {
        /* use the real command that was executed (cmd and lastamc) may be
         * different, in case of MULTI-EXEC or re-written commands such as
         * EXPIRE, GEOADD, etc. */
        real_cmd->microseconds += duration;
        real_cmd->calls++;
    }

    /* Propagate the command into the AOF and replication link */
    if (flags & CMD_CALL_PROPAGATE &&
        (c->flags & CLIENT_PREVENT_PROP) != CLIENT_PREVENT_PROP)
    {
        int propagate_flags = PROPAGATE_NONE;

        /* Check if the command operated changes in the data set. If so
         * set for replication / AOF propagation. */
        if (dirty)
            propagate_flags |= (PROPAGATE_AOF | PROPAGATE_REPL);

        /* If the client forced AOF / replication of the command, set
         * the flags regardless of the command effects on the data set. */
        if (c->flags & CLIENT_FORCE_REPL)
            propagate_flags |= PROPAGATE_REPL;
        if (c->flags & CLIENT_FORCE_AOF)
            propagate_flags |= PROPAGATE_AOF;

        /* However prevent AOF / replication propagation if the command
         * implementation called preventCommandPropagation() or similar,
         * or if we don't have the call() flags to do so. */
        if (c->flags & CLIENT_PREVENT_REPL_PROP ||
            !(flags & CMD_CALL_PROPAGATE_REPL))
            propagate_flags &= ~PROPAGATE_REPL;
        if (c->flags & CLIENT_PREVENT_AOF_PROP ||
            !(flags & CMD_CALL_PROPAGATE_AOF))
            propagate_flags &= ~PROPAGATE_AOF;

        /* Call propagate() only if at least one of AOF / replication
         * propagation is needed. Note that modules commands handle replication
         * in an explicit way, so we never replicate them automatically. */
        if (propagate_flags != PROPAGATE_NONE && !(c->cmd->flags & CMD_MODULE))
            propagate(c->cmd, c->db->id, c->argv, c->argc, propagate_flags);
    }

    /* Restore the old replication flags, since call() can be executed
     * recursively. */
    c->flags &= ~(CLIENT_FORCE_AOF | CLIENT_FORCE_REPL | CLIENT_PREVENT_PROP);
    c->flags |= client_old_flags &
                (CLIENT_FORCE_AOF | CLIENT_FORCE_REPL | CLIENT_PREVENT_PROP);

    /* Handle the alsoPropagate() API to handle commands that want to propagate
     * multiple separated commands. Note that alsoPropagate() is not affected
     * by CLIENT_PREVENT_PROP flag. */
    if (server.also_propagate.numops)
    {
        int j;
        redisOp *rop;

        if (flags & CMD_CALL_PROPAGATE)
        {
            int multi_emitted = 0;
            /* Wrap the commands in server.also_propagate array,
             * but don't wrap it if we are already in MULTI context,
             * in case the nested MULTI/EXEC.
             *
             * And if the array contains only one command, no need to
             * wrap it, since the single command is atomic. */
            if (server.also_propagate.numops > 1 &&
                !(c->cmd->flags & CMD_MODULE) &&
                !(c->flags & CLIENT_MULTI) &&
                !(flags & CMD_CALL_NOWRAP))
            {
                execCommandPropagateMulti(c);
                multi_emitted = 1;
            }

            for (j = 0; j < server.also_propagate.numops; j++)
            {
                rop = &server.also_propagate.ops[j];
                int target = rop->target;
                /* Whatever the command wish is, we honor the call() flags. */
                if (!(flags & CMD_CALL_PROPAGATE_AOF))
                    target &= ~PROPAGATE_AOF;
                if (!(flags & CMD_CALL_PROPAGATE_REPL))
                    target &= ~PROPAGATE_REPL;
                if (target)
                    propagate(rop->cmd, rop->dbid, rop->argv, rop->argc, target);
            }

            if (multi_emitted)
            {
                execCommandPropagateExec(c);
            }
        }
        redisOpArrayFree(&server.also_propagate);
    }
    server.also_propagate = prev_also_propagate;

    /* If the client has keys tracking enabled for client side caching,
     * make sure to remember the keys it fetched via this command. */
    if (c->cmd->flags & CMD_READONLY)
    {
        client *caller = (c->flags & CLIENT_LUA && server.lua_caller) ? server.lua_caller : c;
        if (caller->flags & CLIENT_TRACKING &&
            !(caller->flags & CLIENT_TRACKING_BCAST))
        {
            trackingRememberKeys(caller);
        }
    }

    server.fixed_time_expire--;
    server.stat_numcommands++;

    /* Record peak memory after each command and before the eviction that runs
     * before the next command. */
    size_t zmalloc_used = zmalloc_used_memory();
    if (zmalloc_used > server.stat_peak_memory)
        server.stat_peak_memory = zmalloc_used;
}

/* Used when a command that is ready for execution needs to be rejected, due to
 * varios pre-execution checks. it returns the appropriate error to the client.
 * If there's a transaction is flags it as dirty, and if the command is EXEC,
 * it aborts the transaction.
 * Note: 'reply' is expected to end with \r\n */
void rejectCommand(client *c, robj *reply)
{
    flagTransaction(c);
    if (c->cmd && c->cmd->proc == execCommand)
    {
        execCommandAbort(c, reply->ptr);
    }
    else
    {
        /* using addReplyError* rather than addReply so that the error can be logged. */
        addReplyErrorObject(c, reply);
    }
}

void rejectCommandFormat(client *c, const char *fmt, ...)
{
    flagTransaction(c);
    va_list ap;
    va_start(ap, fmt);
    sds s = sdscatvprintf(sdsempty(), fmt, ap);
    va_end(ap);
    /* Make sure there are no newlines in the string, otherwise invalid protocol
     * is emitted (The args come from the user, they may contain any character). */
    sdsmapchars(s, "\r\n", "  ", 2);
    if (c->cmd && c->cmd->proc == execCommand)
    {
        execCommandAbort(c, s);
    }
    else
    {
        addReplyErrorSds(c, s);
    }
    sdsfree(s);
}

/* If this function gets called we already read a whole
 * command, arguments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 * 如果调用此函数，表明我们已经读取了整个命令，
 * 参数位于客户端 argv/argc 字段中。
 * processCommand() 执行命令或准备从客户端进行批量读取。
 *
 * If C_OK is returned the client is still alive and valid and
 * other operations can be performed by the caller. Otherwise
 * if C_ERR is returned the client was destroyed (i.e. after QUIT).
 * 如果返回 C_OK，则客户端仍然活着且有效，调用者可以执行其他操作。
 * 否则，如果返回 C_ERR，则客户端被销毁（即在 QUIT 之后）。*/
int processCommand(client *c)
{
    moduleCallCommandFilters(c);

    /* The QUIT command is handled separately. Normal command procs will
     * go through checking for replication and QUIT will cause trouble
     * when FORCE_REPLICATION is enabled and would be implemented in
     * a regular command proc.
     * QUIT 命令单独处理。 正常的命令过程将检查复制，
     * 当启用 FORCE_REPLICATION 时 QUIT 将导致问题，并将在常规命令过程中实现。*/
    if (!strcasecmp(c->argv[0]->ptr, "quit"))
    {
        addReply(c, shared.ok);
        c->flags |= CLIENT_CLOSE_AFTER_REPLY;
        return C_ERR;
    }

    /* Now lookup the command and check ASAP about trivial error conditions
     * such as wrong arity, bad command name and so forth.
     现在查找命令并尽快检查琐碎的错误情况，例如错误的数量、错误的命令名称等。 */
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]->ptr);
    if (!c->cmd)
    {
        //找不到命令
        sds args = sdsempty();
        int i;
        for (i = 1; i < c->argc && sdslen(args) < 128; i++)
            args = sdscatprintf(args, "`%.*s`, ", 128 - (int)sdslen(args), (char *)c->argv[i]->ptr);
        rejectCommandFormat(c, "unknown command `%s`, with args beginning with: %s",
                            (char *)c->argv[0]->ptr, args);
        sdsfree(args);
        return C_OK;
    }
    else if ((c->cmd->arity > 0 && c->cmd->arity != c->argc) ||
             (c->argc < -c->cmd->arity))
    {
        //参数数量不对
        rejectCommandFormat(c, "wrong number of arguments for '%s' command",
                            c->cmd->name);
        return C_OK;
    }

    int is_write_command = (c->cmd->flags & CMD_WRITE) ||
                           (c->cmd->proc == execCommand && (c->mstate.cmd_flags & CMD_WRITE));
    int is_denyoom_command = (c->cmd->flags & CMD_DENYOOM) ||
                             (c->cmd->proc == execCommand && (c->mstate.cmd_flags & CMD_DENYOOM));
    int is_denystale_command = !(c->cmd->flags & CMD_STALE) ||
                               (c->cmd->proc == execCommand && (c->mstate.cmd_inv_flags & CMD_STALE));
    int is_denyloading_command = !(c->cmd->flags & CMD_LOADING) ||
                                 (c->cmd->proc == execCommand && (c->mstate.cmd_inv_flags & CMD_LOADING));

    if (authRequired(c))
    {
        /* AUTH and HELLO and no auth commands are valid even in
         * non-authenticated state.
         AUTH 和 HELLO 以及 no auth 命令即使在非身份验证状态下也是有效的。 */
        if (!(c->cmd->flags & CMD_NO_AUTH))
        {
            rejectCommand(c, shared.noautherr);
            return C_OK;
        }
    }

    /* Check if the user can run this command according to the current
     * ACLs. 检查用户是否可以根据当前的 访问控制 运行该命令。*/
    int acl_keypos;
    int acl_retval = ACLCheckCommandPerm(c, &acl_keypos);
    if (acl_retval != ACL_OK)
    {
        //如果当前客户端不能使用该命令
        addACLLogEntry(c, acl_retval, acl_keypos, NULL);
        if (acl_retval == ACL_DENIED_CMD)
            rejectCommandFormat(c,
                                "-NOPERM this user has no permissions to run "
                                "the '%s' command or its subcommand",
                                c->cmd->name);
        else
            rejectCommandFormat(c,
                                "-NOPERM this user has no permissions to access "
                                "one of the keys used as arguments");
        return C_OK;
    }

    /* If cluster is enabled perform the cluster redirection here.
     * However we don't perform the redirection if:
     * 如果启用了集群，请在此处执行集群重定向。 但是在以下情况，如果我们不执行重定向
     *
     * 1) The sender of this command is our master.
     * 2) The command has no key arguments.
     * 1) 命令的发送人是主节点
     * 2) 该命令没有关键参数。
     * */
    if (server.cluster_enabled &&
        !(c->flags & CLIENT_MASTER) &&
        !(c->flags & CLIENT_LUA &&
          server.lua_caller->flags & CLIENT_MASTER) &&
        !(c->cmd->getkeys_proc == NULL && c->cmd->firstkey == 0 &&
          c->cmd->proc != execCommand))
    {
        //集群重定向
        int hashslot;
        int error_code;
        clusterNode *n = getNodeByQuery(c, c->cmd, c->argv, c->argc,
                                        &hashslot, &error_code);
        if (n == NULL || n != server.cluster->myself)
        {
            if (c->cmd->proc == execCommand)
            {
                discardTransaction(c);
            }
            else
            {
                flagTransaction(c);
            }
            clusterRedirectClient(c, n, hashslot, error_code);
            return C_OK;
        }
    }

    /* Handle the maxmemory directive.
     * 处理 maxmemory 指令。

     * Note that we do not want to reclaim memory if we are here re-entering
     * the event loop since there is a busy Lua script running in timeout
     * condition, to avoid mixing the propagation of scripts with the
     * propagation of DELs due to eviction.
     * 注意：我们不想回收内存如果我们是在Lua脚本繁忙超时之后重新进入事件循环，
     * 这避免由于驱逐而将脚本的传播与 DEL 的传播混为一谈。*/
    if (server.maxmemory && !server.lua_timedout)
    {
        int out_of_memory = freeMemoryIfNeededAndSafe() == C_ERR;
        /* freeMemoryIfNeeded may flush slave output buffers. This may result
         * into a slave, that may be the active client, to be freed.
         freeMemoryIfNeeded函数会刷新从节点输出缓冲区，这可能导致活跃的客户端被释放
          */
        if (server.current_client == NULL)
            return C_ERR;

        int reject_cmd_on_oom = is_denyoom_command;
        /* If client is in MULTI/EXEC context, queuing may consume an unlimited
         * amount of memory, so we want to stop that.
         * However, we never want to reject DISCARD, or even EXEC (unless it
         * contains denied commands, in which case is_denyoom_command is already
         * set.
         * 如果客户端在MULTI/EXEC上下文中，排队可能会消耗无限量的内存，所以我们想阻止它。
         * 但是，我们永远不想拒绝 DISCARD，甚至 EXEC（除非它包含被拒绝的命令，在这种情况下 is_denyoom_command 已经设置。）
         * */
        if (c->flags & CLIENT_MULTI &&
            c->cmd->proc != execCommand &&
            c->cmd->proc != discardCommand)
        {
            reject_cmd_on_oom = 1;
        }

        if (out_of_memory && reject_cmd_on_oom)
        {
            //如果内存已经满了，并且命令类型不是execCommand和discardCommand
            rejectCommand(c, shared.oomerr);
            return C_OK;
        }

        /* Save out_of_memory result at script start, otherwise if we check OOM
         * until first write within script, memory used by lua stack and
         * arguments might interfere.
         * 在脚本的开始保存 内存溢出 结果，否则，如果我们在第一次写入脚本之前检查 OOM，lua 堆栈和参数使用的内存可能会干扰
         * */
        if (c->cmd->proc == evalCommand || c->cmd->proc == evalShaCommand)
        {
            server.lua_oom = out_of_memory;
        }
    }

    /* Make sure to use a reasonable amount of memory for client side
     * caching metadata. 确保为客户端缓存数据使用合理大小的内存 */
    if (server.tracking_clients)
        trackingLimitUsedSlots();

    /* Don't accept write commands if there are problems persisting on disk
     * and if this is a master instance.
     当磁盘出现问题和本节点是主节点时，不接收写入命令
     */
    int deny_write_type = writeCommandsDeniedByDiskError();
    if (deny_write_type != DISK_ERROR_TYPE_NONE && server.masterhost == NULL &&
        (is_write_command || c->cmd->proc == pingCommand))
    {
        if (deny_write_type == DISK_ERROR_TYPE_RDB)
            rejectCommand(c, shared.bgsaveerr);
        else
            rejectCommandFormat(c,
                                "-MISCONF Errors writing to the AOF file: %s",
                                strerror(server.aof_last_write_errno));
        return C_OK;
    }

    /* Don't accept write commands if there are not enough good slaves and
     * user configured the min-slaves-to-write option.
     当我们没有足够的从节点符合我们配置的最小写入从节点选项时，不接收写入命令
     */
    if (server.masterhost == NULL && server.repl_min_slaves_to_write && server.repl_min_slaves_max_lag && is_write_command &&
        server.repl_good_slaves_count < server.repl_min_slaves_to_write)
    {
        rejectCommand(c, shared.noreplicaserr);
        return C_OK;
    }

    /* Don't accept write commands if this is a read only slave. But
     * accept write commands if this is our master.
     * 如果这是一个只读从节点，不接收写命令，但是如果是主节点，则没问题。
     */
    if (server.masterhost && server.repl_slave_ro && !(c->flags & CLIENT_MASTER) && is_write_command)
    {
        rejectCommand(c, shared.roslaveerr);
        return C_OK;
    }

    /* Only allow a subset of commands in the context of Pub/Sub if the
     * connection is in RESP2 mode. With RESP3 there are no limits.
     * 如果连接处于 RESP2 模式，则仅允许 Pub/Sub 上下文中的命令子集。 RESP3 没有限制。
     */
    if ((c->flags & CLIENT_PUBSUB && c->resp == 2) &&
        c->cmd->proc != pingCommand &&
        c->cmd->proc != subscribeCommand &&
        c->cmd->proc != unsubscribeCommand &&
        c->cmd->proc != psubscribeCommand &&
        c->cmd->proc != punsubscribeCommand)
    {
        rejectCommandFormat(c,
                            "Can't execute '%s': only (P)SUBSCRIBE / "
                            "(P)UNSUBSCRIBE / PING / QUIT are allowed in this context",
                            c->cmd->name);
        return C_OK;
    }

    /* Only allow commands with flag "t", such as INFO, SLAVEOF and so on,
     * when slave-serve-stale-data is no and we are a slave with a broken
     * link with master.
     * 仅当 slave-serve-stale-data 为 no 且我们是与 master 的链接断开的 slave 时，才允许带有标志“t”的命令，例如 INFO、SLAVEOF 等。
     * */
    if (server.masterhost && server.repl_state != REPL_STATE_CONNECTED && server.repl_serve_stale_data == 0 && is_denystale_command)
    {
        rejectCommand(c, shared.masterdownerr);
        return C_OK;
    }

    /* Loading DB? Return an error if the command has not the
     * CMD_LOADING flag.
     如果命令不带有CMD_LOADING标志，而我们处于loading DB状态，返回错误*/
    if (server.loading && is_denyloading_command)
    {
        rejectCommand(c, shared.loadingerr);
        return C_OK;
    }

    /* Lua script too slow? Only allow a limited number of commands.
     * Note that we need to allow the transactions commands, otherwise clients
     * sending a transaction with pipelining without error checking, may have
     * the MULTI plus a few initial commands refused, then the timeout
     * condition resolves, and the bottom-half of the transaction gets
     * executed, see Github PR #7022.
     *
     * Lua脚本是否太慢，只允许限制数量的命令。
     * 注意：我们需要去允许事务命令，或者客户端发送一个带有流水线无错误检查的事务。
     * 可能有 MULTI 加上一些初始命令被拒绝，然后超时条件解决，事务的下半部分被执行，请参阅 Github PR #7022。
     * */
    if (server.lua_timedout &&
        c->cmd->proc != authCommand &&
        c->cmd->proc != helloCommand &&
        c->cmd->proc != replconfCommand &&
        c->cmd->proc != multiCommand &&
        c->cmd->proc != discardCommand &&
        c->cmd->proc != watchCommand &&
        c->cmd->proc != unwatchCommand &&
        !(c->cmd->proc == shutdownCommand &&
          c->argc == 2 &&
          tolower(((char *)c->argv[1]->ptr)[0]) == 'n') &&
        !(c->cmd->proc == scriptCommand &&
          c->argc == 2 &&
          tolower(((char *)c->argv[1]->ptr)[0]) == 'k'))
    {
        rejectCommand(c, shared.slowscripterr);
        return C_OK;
    }

    /* Exec the command 执行命令*/
    if (c->flags & CLIENT_MULTI && c->cmd->proc != execCommand && c->cmd->proc != discardCommand && c->cmd->proc != multiCommand && c->cmd->proc != watchCommand)
    {
        //此客户端处于 MULTI 事务上下文中，我们入队命令，让命令原子执行
        queueMultiCommand(c);
        addReply(c, shared.queued);
    }
    else
    {
        //执行单个命令
        call(c, CMD_CALL_FULL);
        c->woff = server.master_repl_offset;
        if (listLength(server.ready_keys))
            handleClientsBlockedOnKeys();
    }
    return C_OK;
}

/*================================== Shutdown =============================== */

/* Close listening sockets. Also unlink the unix domain socket if
 * unlink_unix_socket is non-zero. */
void closeListeningSockets(int unlink_unix_socket)
{
    int j;

    for (j = 0; j < server.ipfd_count; j++)
        close(server.ipfd[j]);
    for (j = 0; j < server.tlsfd_count; j++)
        close(server.tlsfd[j]);
    if (server.sofd != -1)
        close(server.sofd);
    if (server.cluster_enabled)
        for (j = 0; j < server.cfd_count; j++)
            close(server.cfd[j]);
    if (unlink_unix_socket && server.unixsocket)
    {
        serverLog(LL_NOTICE, "Removing the unix socket file.");
        unlink(server.unixsocket); /* don't care if this fails */
    }
}

int prepareForShutdown(int flags)
{
    /* When SHUTDOWN is called while the server is loading a dataset in
     * memory we need to make sure no attempt is performed to save
     * the dataset on shutdown (otherwise it could overwrite the current DB
     * with half-read data).
     *
     * Also when in Sentinel mode clear the SAVE flag and force NOSAVE. */
    if (server.loading || server.sentinel_mode)
        flags = (flags & ~SHUTDOWN_SAVE) | SHUTDOWN_NOSAVE;

    int save = flags & SHUTDOWN_SAVE;
    int nosave = flags & SHUTDOWN_NOSAVE;

    serverLog(LL_WARNING, "User requested shutdown...");
    if (server.supervised_mode == SUPERVISED_SYSTEMD)
        redisCommunicateSystemd("STOPPING=1\n");

    /* Kill all the Lua debugger forked sessions. */
    ldbKillForkedSessions();

    /* Kill the saving child if there is a background saving in progress.
       We want to avoid race conditions, for instance our saving child may
       overwrite the synchronous saving did by SHUTDOWN. */
    if (server.rdb_child_pid != -1)
    {
        serverLog(LL_WARNING, "There is a child saving an .rdb. Killing it!");
        /* Note that, in killRDBChild, we call rdbRemoveTempFile that will
         * do close fd(in order to unlink file actully) in background thread.
         * The temp rdb file fd may won't be closed when redis exits quickly,
         * but OS will close this fd when process exits. */
        killRDBChild();
    }

    /* Kill module child if there is one. */
    if (server.module_child_pid != -1)
    {
        serverLog(LL_WARNING, "There is a module fork child. Killing it!");
        TerminateModuleForkChild(server.module_child_pid, 0);
    }

    if (server.aof_state != AOF_OFF)
    {
        /* Kill the AOF saving child as the AOF we already have may be longer
         * but contains the full dataset anyway. */
        if (server.aof_child_pid != -1)
        {
            /* If we have AOF enabled but haven't written the AOF yet, don't
             * shutdown or else the dataset will be lost. */
            if (server.aof_state == AOF_WAIT_REWRITE)
            {
                serverLog(LL_WARNING, "Writing initial AOF, can't exit.");
                return C_ERR;
            }
            serverLog(LL_WARNING,
                      "There is a child rewriting the AOF. Killing it!");
            killAppendOnlyChild();
        }
        /* Append only file: flush buffers and fsync() the AOF at exit */
        serverLog(LL_NOTICE, "Calling fsync() on the AOF file.");
        flushAppendOnlyFile(1);
        redis_fsync(server.aof_fd);
    }

    /* Create a new RDB file before exiting. */
    if ((server.saveparamslen > 0 && !nosave) || save)
    {
        serverLog(LL_NOTICE, "Saving the final RDB snapshot before exiting.");
        if (server.supervised_mode == SUPERVISED_SYSTEMD)
            redisCommunicateSystemd("STATUS=Saving the final RDB snapshot\n");
        /* Snapshotting. Perform a SYNC SAVE and exit */
        rdbSaveInfo rsi, *rsiptr;
        rsiptr = rdbPopulateSaveInfo(&rsi);
        if (rdbSave(server.rdb_filename, rsiptr) != C_OK)
        {
            /* Ooops.. error saving! The best we can do is to continue
             * operating. Note that if there was a background saving process,
             * in the next cron() Redis will be notified that the background
             * saving aborted, handling special stuff like slaves pending for
             * synchronization... */
            serverLog(LL_WARNING, "Error trying to save the DB, can't exit.");
            if (server.supervised_mode == SUPERVISED_SYSTEMD)
                redisCommunicateSystemd("STATUS=Error trying to save the DB, can't exit.\n");
            return C_ERR;
        }
    }

    /* Fire the shutdown modules event. */
    moduleFireServerEvent(REDISMODULE_EVENT_SHUTDOWN, 0, NULL);

    /* Remove the pid file if possible and needed. */
    if (server.daemonize || server.pidfile)
    {
        serverLog(LL_NOTICE, "Removing the pid file.");
        unlink(server.pidfile);
    }

    /* Best effort flush of slave output buffers, so that we hopefully
     * send them pending writes. */
    flushSlavesOutputBuffers();

    /* Close the listening sockets. Apparently this allows faster restarts. */
    closeListeningSockets(1);
    serverLog(LL_WARNING, "%s is now ready to exit, bye bye...",
              server.sentinel_mode ? "Sentinel" : "Redis");
    return C_OK;
}

/*================================== Commands =============================== */

/* Sometimes Redis cannot accept write commands because there is a persistence
 * error with the RDB or AOF file, and Redis is configured in order to stop
 * accepting writes in such situation. This function returns if such a
 * condition is active, and the type of the condition.
 *
 * Function return values:
 *
 * DISK_ERROR_TYPE_NONE:    No problems, we can accept writes.
 * DISK_ERROR_TYPE_AOF:     Don't accept writes: AOF errors.
 * DISK_ERROR_TYPE_RDB:     Don't accept writes: RDB errors.
 */
int writeCommandsDeniedByDiskError(void)
{
    if (server.stop_writes_on_bgsave_err &&
        server.saveparamslen > 0 &&
        server.lastbgsave_status == C_ERR)
    {
        return DISK_ERROR_TYPE_RDB;
    }
    else if (server.aof_state != AOF_OFF &&
             server.aof_last_write_status == C_ERR)
    {
        return DISK_ERROR_TYPE_AOF;
    }
    else
    {
        return DISK_ERROR_TYPE_NONE;
    }
}

/* The PING command. It works in a different way if the client is in
 * in Pub/Sub mode. */
void pingCommand(client *c)
{
    /* The command takes zero or one arguments. */
    if (c->argc > 2)
    {
        addReplyErrorFormat(c, "wrong number of arguments for '%s' command",
                            c->cmd->name);
        return;
    }

    if (c->flags & CLIENT_PUBSUB && c->resp == 2)
    {
        addReply(c, shared.mbulkhdr[2]);
        addReplyBulkCBuffer(c, "pong", 4);
        if (c->argc == 1)
            addReplyBulkCBuffer(c, "", 0);
        else
            addReplyBulk(c, c->argv[1]);
    }
    else
    {
        if (c->argc == 1)
            addReply(c, shared.pong);
        else
            addReplyBulk(c, c->argv[1]);
    }
}

void echoCommand(client *c)
{
    addReplyBulk(c, c->argv[1]);
}

void timeCommand(client *c)
{
    struct timeval tv;

    /* gettimeofday() can only fail if &tv is a bad address so we
     * don't check for errors. */
    gettimeofday(&tv, NULL);
    addReplyArrayLen(c, 2);
    addReplyBulkLongLong(c, tv.tv_sec);
    addReplyBulkLongLong(c, tv.tv_usec);
}

/* Helper function for addReplyCommand() to output flags. */
int addReplyCommandFlag(client *c, struct redisCommand *cmd, int f, char *reply)
{
    if (cmd->flags & f)
    {
        addReplyStatus(c, reply);
        return 1;
    }
    return 0;
}

/* Output the representation of a Redis command. Used by the COMMAND command. */
void addReplyCommand(client *c, struct redisCommand *cmd)
{
    if (!cmd)
    {
        addReplyNull(c);
    }
    else
    {
        /* We are adding: command name, arg count, flags, first, last, offset, categories */
        addReplyArrayLen(c, 7);
        addReplyBulkCString(c, cmd->name);
        addReplyLongLong(c, cmd->arity);

        int flagcount = 0;
        void *flaglen = addReplyDeferredLen(c);
        flagcount += addReplyCommandFlag(c, cmd, CMD_WRITE, "write");
        flagcount += addReplyCommandFlag(c, cmd, CMD_READONLY, "readonly");
        flagcount += addReplyCommandFlag(c, cmd, CMD_DENYOOM, "denyoom");
        flagcount += addReplyCommandFlag(c, cmd, CMD_ADMIN, "admin");
        flagcount += addReplyCommandFlag(c, cmd, CMD_PUBSUB, "pubsub");
        flagcount += addReplyCommandFlag(c, cmd, CMD_NOSCRIPT, "noscript");
        flagcount += addReplyCommandFlag(c, cmd, CMD_RANDOM, "random");
        flagcount += addReplyCommandFlag(c, cmd, CMD_SORT_FOR_SCRIPT, "sort_for_script");
        flagcount += addReplyCommandFlag(c, cmd, CMD_LOADING, "loading");
        flagcount += addReplyCommandFlag(c, cmd, CMD_STALE, "stale");
        flagcount += addReplyCommandFlag(c, cmd, CMD_SKIP_MONITOR, "skip_monitor");
        flagcount += addReplyCommandFlag(c, cmd, CMD_SKIP_SLOWLOG, "skip_slowlog");
        flagcount += addReplyCommandFlag(c, cmd, CMD_ASKING, "asking");
        flagcount += addReplyCommandFlag(c, cmd, CMD_FAST, "fast");
        flagcount += addReplyCommandFlag(c, cmd, CMD_NO_AUTH, "no_auth");
        if ((cmd->getkeys_proc && !(cmd->flags & CMD_MODULE)) ||
            cmd->flags & CMD_MODULE_GETKEYS)
        {
            addReplyStatus(c, "movablekeys");
            flagcount += 1;
        }
        setDeferredSetLen(c, flaglen, flagcount);

        addReplyLongLong(c, cmd->firstkey);
        addReplyLongLong(c, cmd->lastkey);
        addReplyLongLong(c, cmd->keystep);

        addReplyCommandCategories(c, cmd);
    }
}

/* COMMAND <subcommand> <args> */
void commandCommand(client *c)
{
    dictIterator *di;
    dictEntry *de;

    if (c->argc == 2 && !strcasecmp(c->argv[1]->ptr, "help"))
    {
        const char *help[] = {
            "(no subcommand) -- Return details about all Redis commands.",
            "COUNT -- Return the total number of commands in this Redis server.",
            "GETKEYS <full-command> -- Return the keys from a full Redis command.",
            "INFO [command-name ...] -- Return details about multiple Redis commands.",
            NULL};
        addReplyHelp(c, help);
    }
    else if (c->argc == 1)
    {
        addReplyArrayLen(c, dictSize(server.commands));
        di = dictGetIterator(server.commands);
        while ((de = dictNext(di)) != NULL)
        {
            addReplyCommand(c, dictGetVal(de));
        }
        dictReleaseIterator(di);
    }
    else if (!strcasecmp(c->argv[1]->ptr, "info"))
    {
        int i;
        addReplyArrayLen(c, c->argc - 2);
        for (i = 2; i < c->argc; i++)
        {
            addReplyCommand(c, dictFetchValue(server.commands, c->argv[i]->ptr));
        }
    }
    else if (!strcasecmp(c->argv[1]->ptr, "count") && c->argc == 2)
    {
        addReplyLongLong(c, dictSize(server.commands));
    }
    else if (!strcasecmp(c->argv[1]->ptr, "getkeys") && c->argc >= 3)
    {
        struct redisCommand *cmd = lookupCommand(c->argv[2]->ptr);
        getKeysResult result = GETKEYS_RESULT_INIT;
        int j;

        if (!cmd)
        {
            addReplyError(c, "Invalid command specified");
            return;
        }
        else if (cmd->getkeys_proc == NULL && cmd->firstkey == 0)
        {
            addReplyError(c, "The command has no key arguments");
            return;
        }
        else if ((cmd->arity > 0 && cmd->arity != c->argc - 2) ||
                 ((c->argc - 2) < -cmd->arity))
        {
            addReplyError(c, "Invalid number of arguments specified for command");
            return;
        }

        if (!getKeysFromCommand(cmd, c->argv + 2, c->argc - 2, &result))
        {
            addReplyError(c, "Invalid arguments specified for command");
        }
        else
        {
            addReplyArrayLen(c, result.numkeys);
            for (j = 0; j < result.numkeys; j++)
                addReplyBulk(c, c->argv[result.keys[j] + 2]);
        }
        getKeysFreeResult(&result);
    }
    else
    {
        addReplySubcommandSyntaxError(c);
    }
}

/* Convert an amount of bytes into a human readable string in the form
 * of 100B, 2G, 100M, 4K, and so forth. */
void bytesToHuman(char *s, unsigned long long n)
{
    double d;

    if (n < 1024)
    {
        /* Bytes */
        sprintf(s, "%lluB", n);
    }
    else if (n < (1024 * 1024))
    {
        d = (double)n / (1024);
        sprintf(s, "%.2fK", d);
    }
    else if (n < (1024LL * 1024 * 1024))
    {
        d = (double)n / (1024 * 1024);
        sprintf(s, "%.2fM", d);
    }
    else if (n < (1024LL * 1024 * 1024 * 1024))
    {
        d = (double)n / (1024LL * 1024 * 1024);
        sprintf(s, "%.2fG", d);
    }
    else if (n < (1024LL * 1024 * 1024 * 1024 * 1024))
    {
        d = (double)n / (1024LL * 1024 * 1024 * 1024);
        sprintf(s, "%.2fT", d);
    }
    else if (n < (1024LL * 1024 * 1024 * 1024 * 1024 * 1024))
    {
        d = (double)n / (1024LL * 1024 * 1024 * 1024 * 1024);
        sprintf(s, "%.2fP", d);
    }
    else
    {
        /* Let's hope we never need this */
        sprintf(s, "%lluB", n);
    }
}

/* Create the string returned by the INFO command. This is decoupled
 * by the INFO command itself as we need to report the same information
 * on memory corruption problems. */
sds genRedisInfoString(const char *section)
{
    sds info = sdsempty();
    time_t uptime = server.unixtime - server.stat_starttime;
    int j;
    struct rusage self_ru, c_ru;
    int allsections = 0, defsections = 0, everything = 0, modules = 0;
    int sections = 0;

    if (section == NULL)
        section = "default";
    allsections = strcasecmp(section, "all") == 0;
    defsections = strcasecmp(section, "default") == 0;
    everything = strcasecmp(section, "everything") == 0;
    modules = strcasecmp(section, "modules") == 0;
    if (everything)
        allsections = 1;

    getrusage(RUSAGE_SELF, &self_ru);
    getrusage(RUSAGE_CHILDREN, &c_ru);

    /* Server */
    if (allsections || defsections || !strcasecmp(section, "server"))
    {
        static int call_uname = 1;
        static struct utsname name;
        char *mode;

        if (server.cluster_enabled)
            mode = "cluster";
        else if (server.sentinel_mode)
            mode = "sentinel";
        else
            mode = "standalone";

        if (sections++)
            info = sdscat(info, "\r\n");

        if (call_uname)
        {
            /* Uname can be slow and is always the same output. Cache it. */
            uname(&name);
            call_uname = 0;
        }

        info = sdscatfmt(info,
                         "# Server\r\n"
                         "redis_version:%s\r\n"
                         "redis_git_sha1:%s\r\n"
                         "redis_git_dirty:%i\r\n"
                         "redis_build_id:%s\r\n"
                         "redis_mode:%s\r\n"
                         "os:%s %s %s\r\n"
                         "arch_bits:%i\r\n"
                         "multiplexing_api:%s\r\n"
                         "atomicvar_api:%s\r\n"
                         "gcc_version:%i.%i.%i\r\n"
                         "process_id:%I\r\n"
                         "run_id:%s\r\n"
                         "tcp_port:%i\r\n"
                         "uptime_in_seconds:%I\r\n"
                         "uptime_in_days:%I\r\n"
                         "hz:%i\r\n"
                         "configured_hz:%i\r\n"
                         "lru_clock:%u\r\n"
                         "executable:%s\r\n"
                         "config_file:%s\r\n"
                         "io_threads_active:%i\r\n",
                         REDIS_VERSION,
                         redisGitSHA1(),
                         strtol(redisGitDirty(), NULL, 10) > 0,
                         redisBuildIdString(),
                         mode,
                         name.sysname, name.release, name.machine,
                         server.arch_bits,
                         aeGetApiName(),
                         REDIS_ATOMIC_API,
#ifdef __GNUC__
                         __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__,
#else
                         0, 0, 0,
#endif
                         (int64_t)getpid(),
                         server.runid,
                         server.port ? server.port : server.tls_port,
                         (int64_t)uptime,
                         (int64_t)(uptime / (3600 * 24)),
                         server.hz,
                         server.config_hz,
                         server.lruclock,
                         server.executable ? server.executable : "",
                         server.configfile ? server.configfile : "",
                         server.io_threads_active);
    }

    /* Clients */
    if (allsections || defsections || !strcasecmp(section, "clients"))
    {
        size_t maxin, maxout;
        getExpansiveClientsInfo(&maxin, &maxout);
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# Clients\r\n"
                            "connected_clients:%lu\r\n"
                            "client_recent_max_input_buffer:%zu\r\n"
                            "client_recent_max_output_buffer:%zu\r\n"
                            "blocked_clients:%d\r\n"
                            "tracking_clients:%d\r\n"
                            "clients_in_timeout_table:%llu\r\n",
                            listLength(server.clients) - listLength(server.slaves),
                            maxin, maxout,
                            server.blocked_clients,
                            server.tracking_clients,
                            (unsigned long long)raxSize(server.clients_timeout_table));
    }

    /* Memory */
    if (allsections || defsections || !strcasecmp(section, "memory"))
    {
        char hmem[64];
        char peak_hmem[64];
        char total_system_hmem[64];
        char used_memory_lua_hmem[64];
        char used_memory_scripts_hmem[64];
        char used_memory_rss_hmem[64];
        char maxmemory_hmem[64];
        size_t zmalloc_used = zmalloc_used_memory();
        size_t total_system_mem = server.system_memory_size;
        const char *evict_policy = evictPolicyToString();
        long long memory_lua = server.lua ? (long long)lua_gc(server.lua, LUA_GCCOUNT, 0) * 1024 : 0;
        struct redisMemOverhead *mh = getMemoryOverheadData();

        /* Peak memory is updated from time to time by serverCron() so it
         * may happen that the instantaneous value is slightly bigger than
         * the peak value. This may confuse users, so we update the peak
         * if found smaller than the current memory usage. */
        if (zmalloc_used > server.stat_peak_memory)
            server.stat_peak_memory = zmalloc_used;

        bytesToHuman(hmem, zmalloc_used);
        bytesToHuman(peak_hmem, server.stat_peak_memory);
        bytesToHuman(total_system_hmem, total_system_mem);
        bytesToHuman(used_memory_lua_hmem, memory_lua);
        bytesToHuman(used_memory_scripts_hmem, mh->lua_caches);
        bytesToHuman(used_memory_rss_hmem, server.cron_malloc_stats.process_rss);
        bytesToHuman(maxmemory_hmem, server.maxmemory);

        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# Memory\r\n"
                            "used_memory:%zu\r\n"
                            "used_memory_human:%s\r\n"
                            "used_memory_rss:%zu\r\n"
                            "used_memory_rss_human:%s\r\n"
                            "used_memory_peak:%zu\r\n"
                            "used_memory_peak_human:%s\r\n"
                            "used_memory_peak_perc:%.2f%%\r\n"
                            "used_memory_overhead:%zu\r\n"
                            "used_memory_startup:%zu\r\n"
                            "used_memory_dataset:%zu\r\n"
                            "used_memory_dataset_perc:%.2f%%\r\n"
                            "allocator_allocated:%zu\r\n"
                            "allocator_active:%zu\r\n"
                            "allocator_resident:%zu\r\n"
                            "total_system_memory:%lu\r\n"
                            "total_system_memory_human:%s\r\n"
                            "used_memory_lua:%lld\r\n"
                            "used_memory_lua_human:%s\r\n"
                            "used_memory_scripts:%lld\r\n"
                            "used_memory_scripts_human:%s\r\n"
                            "number_of_cached_scripts:%lu\r\n"
                            "maxmemory:%lld\r\n"
                            "maxmemory_human:%s\r\n"
                            "maxmemory_policy:%s\r\n"
                            "allocator_frag_ratio:%.2f\r\n"
                            "allocator_frag_bytes:%zu\r\n"
                            "allocator_rss_ratio:%.2f\r\n"
                            "allocator_rss_bytes:%zd\r\n"
                            "rss_overhead_ratio:%.2f\r\n"
                            "rss_overhead_bytes:%zd\r\n"
                            "mem_fragmentation_ratio:%.2f\r\n"
                            "mem_fragmentation_bytes:%zd\r\n"
                            "mem_not_counted_for_evict:%zu\r\n"
                            "mem_replication_backlog:%zu\r\n"
                            "mem_clients_slaves:%zu\r\n"
                            "mem_clients_normal:%zu\r\n"
                            "mem_aof_buffer:%zu\r\n"
                            "mem_allocator:%s\r\n"
                            "active_defrag_running:%d\r\n"
                            "lazyfree_pending_objects:%zu\r\n",
                            zmalloc_used,
                            hmem,
                            server.cron_malloc_stats.process_rss,
                            used_memory_rss_hmem,
                            server.stat_peak_memory,
                            peak_hmem,
                            mh->peak_perc,
                            mh->overhead_total,
                            mh->startup_allocated,
                            mh->dataset,
                            mh->dataset_perc,
                            server.cron_malloc_stats.allocator_allocated,
                            server.cron_malloc_stats.allocator_active,
                            server.cron_malloc_stats.allocator_resident,
                            (unsigned long)total_system_mem,
                            total_system_hmem,
                            memory_lua,
                            used_memory_lua_hmem,
                            (long long)mh->lua_caches,
                            used_memory_scripts_hmem,
                            dictSize(server.lua_scripts),
                            server.maxmemory,
                            maxmemory_hmem,
                            evict_policy,
                            mh->allocator_frag,
                            mh->allocator_frag_bytes,
                            mh->allocator_rss,
                            mh->allocator_rss_bytes,
                            mh->rss_extra,
                            mh->rss_extra_bytes,
                            mh->total_frag, /* This is the total RSS overhead, including
                                               fragmentation, but not just it. This field
                                               (and the next one) is named like that just
                                               for backward compatibility. */
                            mh->total_frag_bytes,
                            freeMemoryGetNotCountedMemory(),
                            mh->repl_backlog,
                            mh->clients_slaves,
                            mh->clients_normal,
                            mh->aof_buffer,
                            ZMALLOC_LIB,
                            server.active_defrag_running,
                            lazyfreeGetPendingObjectsCount());
        freeMemoryOverheadData(mh);
    }

    /* Persistence */
    if (allsections || defsections || !strcasecmp(section, "persistence"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# Persistence\r\n"
                            "loading:%d\r\n"
                            "rdb_changes_since_last_save:%lld\r\n"
                            "rdb_bgsave_in_progress:%d\r\n"
                            "rdb_last_save_time:%jd\r\n"
                            "rdb_last_bgsave_status:%s\r\n"
                            "rdb_last_bgsave_time_sec:%jd\r\n"
                            "rdb_current_bgsave_time_sec:%jd\r\n"
                            "rdb_last_cow_size:%zu\r\n"
                            "aof_enabled:%d\r\n"
                            "aof_rewrite_in_progress:%d\r\n"
                            "aof_rewrite_scheduled:%d\r\n"
                            "aof_last_rewrite_time_sec:%jd\r\n"
                            "aof_current_rewrite_time_sec:%jd\r\n"
                            "aof_last_bgrewrite_status:%s\r\n"
                            "aof_last_write_status:%s\r\n"
                            "aof_last_cow_size:%zu\r\n"
                            "module_fork_in_progress:%d\r\n"
                            "module_fork_last_cow_size:%zu\r\n",
                            server.loading,
                            server.dirty,
                            server.rdb_child_pid != -1,
                            (intmax_t)server.lastsave,
                            (server.lastbgsave_status == C_OK) ? "ok" : "err",
                            (intmax_t)server.rdb_save_time_last,
                            (intmax_t)((server.rdb_child_pid == -1) ? -1 : time(NULL) - server.rdb_save_time_start),
                            server.stat_rdb_cow_bytes,
                            server.aof_state != AOF_OFF,
                            server.aof_child_pid != -1,
                            server.aof_rewrite_scheduled,
                            (intmax_t)server.aof_rewrite_time_last,
                            (intmax_t)((server.aof_child_pid == -1) ? -1 : time(NULL) - server.aof_rewrite_time_start),
                            (server.aof_lastbgrewrite_status == C_OK) ? "ok" : "err",
                            (server.aof_last_write_status == C_OK) ? "ok" : "err",
                            server.stat_aof_cow_bytes,
                            server.module_child_pid != -1,
                            server.stat_module_cow_bytes);

        if (server.aof_enabled)
        {
            info = sdscatprintf(info,
                                "aof_current_size:%lld\r\n"
                                "aof_base_size:%lld\r\n"
                                "aof_pending_rewrite:%d\r\n"
                                "aof_buffer_length:%zu\r\n"
                                "aof_rewrite_buffer_length:%lu\r\n"
                                "aof_pending_bio_fsync:%llu\r\n"
                                "aof_delayed_fsync:%lu\r\n",
                                (long long)server.aof_current_size,
                                (long long)server.aof_rewrite_base_size,
                                server.aof_rewrite_scheduled,
                                sdslen(server.aof_buf),
                                aofRewriteBufferSize(),
                                bioPendingJobsOfType(BIO_AOF_FSYNC),
                                server.aof_delayed_fsync);
        }

        if (server.loading)
        {
            double perc;
            time_t eta, elapsed;
            off_t remaining_bytes = server.loading_total_bytes -
                                    server.loading_loaded_bytes;

            perc = ((double)server.loading_loaded_bytes /
                    (server.loading_total_bytes + 1)) *
                   100;

            elapsed = time(NULL) - server.loading_start_time;
            if (elapsed == 0)
            {
                eta = 1; /* A fake 1 second figure if we don't have
                            enough info */
            }
            else
            {
                eta = (elapsed * remaining_bytes) / (server.loading_loaded_bytes + 1);
            }

            info = sdscatprintf(info,
                                "loading_start_time:%jd\r\n"
                                "loading_total_bytes:%llu\r\n"
                                "loading_loaded_bytes:%llu\r\n"
                                "loading_loaded_perc:%.2f\r\n"
                                "loading_eta_seconds:%jd\r\n",
                                (intmax_t)server.loading_start_time,
                                (unsigned long long)server.loading_total_bytes,
                                (unsigned long long)server.loading_loaded_bytes,
                                perc,
                                (intmax_t)eta);
        }
    }

    /* Stats */
    if (allsections || defsections || !strcasecmp(section, "stats"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# Stats\r\n"
                            "total_connections_received:%lld\r\n"
                            "total_commands_processed:%lld\r\n"
                            "instantaneous_ops_per_sec:%lld\r\n"
                            "total_net_input_bytes:%lld\r\n"
                            "total_net_output_bytes:%lld\r\n"
                            "instantaneous_input_kbps:%.2f\r\n"
                            "instantaneous_output_kbps:%.2f\r\n"
                            "rejected_connections:%lld\r\n"
                            "sync_full:%lld\r\n"
                            "sync_partial_ok:%lld\r\n"
                            "sync_partial_err:%lld\r\n"
                            "expired_keys:%lld\r\n"
                            "expired_stale_perc:%.2f\r\n"
                            "expired_time_cap_reached_count:%lld\r\n"
                            "expire_cycle_cpu_milliseconds:%lld\r\n"
                            "evicted_keys:%lld\r\n"
                            "keyspace_hits:%lld\r\n"
                            "keyspace_misses:%lld\r\n"
                            "pubsub_channels:%ld\r\n"
                            "pubsub_patterns:%lu\r\n"
                            "latest_fork_usec:%lld\r\n"
                            "migrate_cached_sockets:%ld\r\n"
                            "slave_expires_tracked_keys:%zu\r\n"
                            "active_defrag_hits:%lld\r\n"
                            "active_defrag_misses:%lld\r\n"
                            "active_defrag_key_hits:%lld\r\n"
                            "active_defrag_key_misses:%lld\r\n"
                            "tracking_total_keys:%lld\r\n"
                            "tracking_total_items:%lld\r\n"
                            "tracking_total_prefixes:%lld\r\n"
                            "unexpected_error_replies:%lld\r\n"
                            "total_reads_processed:%lld\r\n"
                            "total_writes_processed:%lld\r\n"
                            "io_threaded_reads_processed:%lld\r\n"
                            "io_threaded_writes_processed:%lld\r\n",
                            server.stat_numconnections,
                            server.stat_numcommands,
                            getInstantaneousMetric(STATS_METRIC_COMMAND),
                            server.stat_net_input_bytes,
                            server.stat_net_output_bytes,
                            (float)getInstantaneousMetric(STATS_METRIC_NET_INPUT) / 1024,
                            (float)getInstantaneousMetric(STATS_METRIC_NET_OUTPUT) / 1024,
                            server.stat_rejected_conn,
                            server.stat_sync_full,
                            server.stat_sync_partial_ok,
                            server.stat_sync_partial_err,
                            server.stat_expiredkeys,
                            server.stat_expired_stale_perc * 100,
                            server.stat_expired_time_cap_reached_count,
                            server.stat_expire_cycle_time_used / 1000,
                            server.stat_evictedkeys,
                            server.stat_keyspace_hits,
                            server.stat_keyspace_misses,
                            dictSize(server.pubsub_channels),
                            listLength(server.pubsub_patterns),
                            server.stat_fork_time,
                            dictSize(server.migrate_cached_sockets),
                            getSlaveKeyWithExpireCount(),
                            server.stat_active_defrag_hits,
                            server.stat_active_defrag_misses,
                            server.stat_active_defrag_key_hits,
                            server.stat_active_defrag_key_misses,
                            (unsigned long long)trackingGetTotalKeys(),
                            (unsigned long long)trackingGetTotalItems(),
                            (unsigned long long)trackingGetTotalPrefixes(),
                            server.stat_unexpected_error_replies,
                            server.stat_total_reads_processed,
                            server.stat_total_writes_processed,
                            server.stat_io_reads_processed,
                            server.stat_io_writes_processed);
    }

    /* Replication */
    if (allsections || defsections || !strcasecmp(section, "replication"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# Replication\r\n"
                            "role:%s\r\n",
                            server.masterhost == NULL ? "master" : "slave");
        if (server.masterhost)
        {
            long long slave_repl_offset = 1;

            if (server.master)
                slave_repl_offset = server.master->reploff;
            else if (server.cached_master)
                slave_repl_offset = server.cached_master->reploff;

            info = sdscatprintf(info,
                                "master_host:%s\r\n"
                                "master_port:%d\r\n"
                                "master_link_status:%s\r\n"
                                "master_last_io_seconds_ago:%d\r\n"
                                "master_sync_in_progress:%d\r\n"
                                "slave_repl_offset:%lld\r\n",
                                server.masterhost,
                                server.masterport,
                                (server.repl_state == REPL_STATE_CONNECTED) ? "up" : "down",
                                server.master ? ((int)(server.unixtime - server.master->lastinteraction)) : -1,
                                server.repl_state == REPL_STATE_TRANSFER,
                                slave_repl_offset);

            if (server.repl_state == REPL_STATE_TRANSFER)
            {
                info = sdscatprintf(info,
                                    "master_sync_left_bytes:%lld\r\n"
                                    "master_sync_last_io_seconds_ago:%d\r\n",
                                    (long long)(server.repl_transfer_size - server.repl_transfer_read),
                                    (int)(server.unixtime - server.repl_transfer_lastio));
            }

            if (server.repl_state != REPL_STATE_CONNECTED)
            {
                info = sdscatprintf(info,
                                    "master_link_down_since_seconds:%jd\r\n",
                                    (intmax_t)(server.unixtime - server.repl_down_since));
            }
            info = sdscatprintf(info,
                                "slave_priority:%d\r\n"
                                "slave_read_only:%d\r\n",
                                server.slave_priority,
                                server.repl_slave_ro);
        }

        info = sdscatprintf(info,
                            "connected_slaves:%lu\r\n",
                            listLength(server.slaves));

        /* If min-slaves-to-write is active, write the number of slaves
         * currently considered 'good'. */
        if (server.repl_min_slaves_to_write &&
            server.repl_min_slaves_max_lag)
        {
            info = sdscatprintf(info,
                                "min_slaves_good_slaves:%d\r\n",
                                server.repl_good_slaves_count);
        }

        if (listLength(server.slaves))
        {
            int slaveid = 0;
            listNode *ln;
            listIter li;

            listRewind(server.slaves, &li);
            while ((ln = listNext(&li)))
            {
                client *slave = listNodeValue(ln);
                char *state = NULL;
                char ip[NET_IP_STR_LEN], *slaveip = slave->slave_ip;
                int port;
                long lag = 0;

                if (slaveip[0] == '\0')
                {
                    if (connPeerToString(slave->conn, ip, sizeof(ip), &port) == -1)
                        continue;
                    slaveip = ip;
                }
                switch (slave->replstate)
                {
                case SLAVE_STATE_WAIT_BGSAVE_START:
                case SLAVE_STATE_WAIT_BGSAVE_END:
                    state = "wait_bgsave";
                    break;
                case SLAVE_STATE_SEND_BULK:
                    state = "send_bulk";
                    break;
                case SLAVE_STATE_ONLINE:
                    state = "online";
                    break;
                }
                if (state == NULL)
                    continue;
                if (slave->replstate == SLAVE_STATE_ONLINE)
                    lag = time(NULL) - slave->repl_ack_time;

                info = sdscatprintf(info,
                                    "slave%d:ip=%s,port=%d,state=%s,"
                                    "offset=%lld,lag=%ld\r\n",
                                    slaveid, slaveip, slave->slave_listening_port, state,
                                    slave->repl_ack_off, lag);
                slaveid++;
            }
        }
        info = sdscatprintf(info,
                            "master_replid:%s\r\n"
                            "master_replid2:%s\r\n"
                            "master_repl_offset:%lld\r\n"
                            "second_repl_offset:%lld\r\n"
                            "repl_backlog_active:%d\r\n"
                            "repl_backlog_size:%lld\r\n"
                            "repl_backlog_first_byte_offset:%lld\r\n"
                            "repl_backlog_histlen:%lld\r\n",
                            server.replid,
                            server.replid2,
                            server.master_repl_offset,
                            server.second_replid_offset,
                            server.repl_backlog != NULL,
                            server.repl_backlog_size,
                            server.repl_backlog_off,
                            server.repl_backlog_histlen);
    }

    /* CPU */
    if (allsections || defsections || !strcasecmp(section, "cpu"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# CPU\r\n"
                            "used_cpu_sys:%ld.%06ld\r\n"
                            "used_cpu_user:%ld.%06ld\r\n"
                            "used_cpu_sys_children:%ld.%06ld\r\n"
                            "used_cpu_user_children:%ld.%06ld\r\n",
                            (long)self_ru.ru_stime.tv_sec, (long)self_ru.ru_stime.tv_usec,
                            (long)self_ru.ru_utime.tv_sec, (long)self_ru.ru_utime.tv_usec,
                            (long)c_ru.ru_stime.tv_sec, (long)c_ru.ru_stime.tv_usec,
                            (long)c_ru.ru_utime.tv_sec, (long)c_ru.ru_utime.tv_usec);
    }

    /* Modules */
    if (allsections || defsections || !strcasecmp(section, "modules"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info, "# Modules\r\n");
        info = genModulesInfoString(info);
    }

    /* Command statistics */
    if (allsections || !strcasecmp(section, "commandstats"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info, "# Commandstats\r\n");

        struct redisCommand *c;
        dictEntry *de;
        dictIterator *di;
        di = dictGetSafeIterator(server.commands);
        while ((de = dictNext(di)) != NULL)
        {
            c = (struct redisCommand *)dictGetVal(de);
            if (!c->calls)
                continue;
            info = sdscatprintf(info,
                                "cmdstat_%s:calls=%lld,usec=%lld,usec_per_call=%.2f\r\n",
                                c->name, c->calls, c->microseconds,
                                (c->calls == 0) ? 0 : ((float)c->microseconds / c->calls));
        }
        dictReleaseIterator(di);
    }

    /* Cluster */
    if (allsections || defsections || !strcasecmp(section, "cluster"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info,
                            "# Cluster\r\n"
                            "cluster_enabled:%d\r\n",
                            server.cluster_enabled);
    }

    /* Key space */
    if (allsections || defsections || !strcasecmp(section, "keyspace"))
    {
        if (sections++)
            info = sdscat(info, "\r\n");
        info = sdscatprintf(info, "# Keyspace\r\n");
        for (j = 0; j < server.dbnum; j++)
        {
            long long keys, vkeys;

            keys = dictSize(server.db[j].dict);
            vkeys = dictSize(server.db[j].expires);
            if (keys || vkeys)
            {
                info = sdscatprintf(info,
                                    "db%d:keys=%lld,expires=%lld,avg_ttl=%lld\r\n",
                                    j, keys, vkeys, server.db[j].avg_ttl);
            }
        }
    }

    /* Get info from modules.
     * if user asked for "everything" or "modules", or a specific section
     * that's not found yet. */
    if (everything || modules ||
        (!allsections && !defsections && sections == 0))
    {
        info = modulesCollectInfo(info,
                                  everything || modules ? NULL : section,
                                  0, /* not a crash report */
                                  sections);
    }
    return info;
}

void infoCommand(client *c)
{
    char *section = c->argc == 2 ? c->argv[1]->ptr : "default";

    if (c->argc > 2)
    {
        addReply(c, shared.syntaxerr);
        return;
    }
    sds info = genRedisInfoString(section);
    addReplyVerbatim(c, info, sdslen(info), "txt");
    sdsfree(info);
}

void monitorCommand(client *c)
{
    /* ignore MONITOR if already slave or in monitor mode */
    if (c->flags & CLIENT_SLAVE)
        return;

    c->flags |= (CLIENT_SLAVE | CLIENT_MONITOR);
    listAddNodeTail(server.monitors, c);
    addReply(c, shared.ok);
}

/* =================================== Main! ================================ */

int checkIgnoreWarning(const char *warning)
{
    int argc, j;
    sds *argv = sdssplitargs(server.ignore_warnings, &argc);
    if (argv == NULL)
        return 0;

    for (j = 0; j < argc; j++)
    {
        char *flag = argv[j];
        if (!strcasecmp(flag, warning))
            break;
    }
    sdsfreesplitres(argv, argc);
    return j < argc;
}

#ifdef __linux__
int linuxOvercommitMemoryValue(void)
{
    FILE *fp = fopen("/proc/sys/vm/overcommit_memory", "r");
    char buf[64];

    if (!fp)
        return -1;
    if (fgets(buf, 64, fp) == NULL)
    {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    return atoi(buf);
}

void linuxMemoryWarnings(void)
{
    /**
     * 0：表示内核将检查是否有足够的可用内存供应用进程使用；如果有足够的可用内存，内存申请允许；否则，内存申请失败，并把错误返回给应用进程。
     * 1：表示内核允许分配所有的物理内存，而不管当前的内存状态如何。
     * 2：表示内核允许分配超过所有物理内存和交换空间总和的内存。
     *
     */
    if (linuxOvercommitMemoryValue() == 0)
    {
        serverLog(LL_WARNING, "WARNING overcommit_memory is set to 0! Background save may fail under low memory condition. To fix this issue add 'vm.overcommit_memory = 1' to /etc/sysctl.conf and then reboot or run the command 'sysctl vm.overcommit_memory=1' for this to take effect.");
    }
    if (THPIsEnabled())
    {
        serverLog(LL_WARNING, "WARNING you have Transparent Huge Pages (THP) support enabled in your kernel. This will create latency and memory usage issues with Redis. To fix this issue run the command 'echo madvise > /sys/kernel/mm/transparent_hugepage/enabled' as root, and add it to your /etc/rc.local in order to retain the setting after a reboot. Redis must be restarted after THP is disabled (set to 'madvise' or 'never').");
    }
}

#ifdef __arm64__

/* Get size in kilobytes of the Shared_Dirty pages of the calling process for the
 * memory map corresponding to the provided address, or -1 on error. */
static int smapsGetSharedDirty(unsigned long addr)
{
    int ret, in_mapping = 0, val = -1;
    unsigned long from, to;
    char buf[64];
    FILE *f;

    f = fopen("/proc/self/smaps", "r");
    if (!f)
        return -1;

    while (1)
    {
        if (!fgets(buf, sizeof(buf), f))
            break;

        ret = sscanf(buf, "%lx-%lx", &from, &to);
        if (ret == 2)
            in_mapping = from <= addr && addr < to;

        if (in_mapping && !memcmp(buf, "Shared_Dirty:", 13))
        {
            sscanf(buf, "%*s %d", &val);
            /* If parsing fails, we remain with val == -1 */
            break;
        }
    }

    fclose(f);
    return val;
}

/* Older arm64 Linux kernels have a bug that could lead to data corruption
 * during background save in certain scenarios. This function checks if the
 * kernel is affected.
 * The bug was fixed in commit ff1712f953e27f0b0718762ec17d0adb15c9fd0b
 * titled: "arm64: pgtable: Ensure dirty bit is preserved across pte_wrprotect()"
 * Return -1 on unexpected test failure, 1 if the kernel seems to be affected,
 * and 0 otherwise.
 * 旧的arm64 Linux内核有一个bug，在某些情况下，在后台保存期间可能会导致数据损坏。
 * 此函数检查内核是否受到影响。测试失败时返回-1，如果内核似乎受到影响，则返回1，否则返回0*/
int linuxMadvFreeForkBugCheck(void)
{
    int ret, pipefd[2] = {-1, -1};
    pid_t pid;
    char *p = NULL, *q;
    int bug_found = 0;
    long page_size = sysconf(_SC_PAGESIZE);
    long map_size = 3 * page_size;

    /* Create a memory map that's in our full control (not one used by the allocator). */
    p = mmap(NULL, map_size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED)
    {
        serverLog(LL_WARNING, "Failed to mmap(): %s", strerror(errno));
        return -1;
    }

    q = p + page_size;

    /* Split the memory map in 3 pages by setting their protection as RO|RW|RO to prevent
     * Linux from merging this memory map with adjacent VMAs. */
    ret = mprotect(q, page_size, PROT_READ | PROT_WRITE);
    if (ret < 0)
    {
        serverLog(LL_WARNING, "Failed to mprotect(): %s", strerror(errno));
        bug_found = -1;
        goto exit;
    }

    /* Write to the page once to make it resident */
    *(volatile char *)q = 0;

    /* Tell the kernel that this page is free to be reclaimed. */
#ifndef MADV_FREE
#define MADV_FREE 8
#endif
    ret = madvise(q, page_size, MADV_FREE);
    if (ret < 0)
    {
        /* MADV_FREE is not available on older kernels that are presumably
         * not affected. */
        if (errno == EINVAL)
            goto exit;

        serverLog(LL_WARNING, "Failed to madvise(): %s", strerror(errno));
        bug_found = -1;
        goto exit;
    }

    /* Write to the page after being marked for freeing, this is supposed to take
     * ownership of that page again. */
    *(volatile char *)q = 0;

    /* Create a pipe for the child to return the info to the parent. */
    ret = pipe(pipefd);
    if (ret < 0)
    {
        serverLog(LL_WARNING, "Failed to create pipe: %s", strerror(errno));
        bug_found = -1;
        goto exit;
    }

    /* Fork the process. */
    pid = fork();
    if (pid < 0)
    {
        serverLog(LL_WARNING, "Failed to fork: %s", strerror(errno));
        bug_found = -1;
        goto exit;
    }
    else if (!pid)
    {
        /* Child: check if the page is marked as dirty, page_size in kb.
         * A value of 0 means the kernel is affected by the bug. */
        ret = smapsGetSharedDirty((unsigned long)q);
        if (!ret)
            bug_found = 1;
        else if (ret == -1) /* Failed to read */
            bug_found = -1;

        if (write(pipefd[1], &bug_found, sizeof(bug_found)) < 0)
            serverLog(LL_WARNING, "Failed to write to parent: %s", strerror(errno));
        exit(0);
    }
    else
    {
        /* Read the result from the child. */
        ret = read(pipefd[0], &bug_found, sizeof(bug_found));
        if (ret < 0)
        {
            serverLog(LL_WARNING, "Failed to read from child: %s", strerror(errno));
            bug_found = -1;
        }

        /* Reap the child pid. */
        waitpid(pid, NULL, 0);
    }

exit:
    /* Cleanup */
    if (pipefd[0] != -1)
        close(pipefd[0]);
    if (pipefd[1] != -1)
        close(pipefd[1]);
    if (p != NULL)
        munmap(p, map_size);

    return bug_found;
}
#endif /* __arm64__ */
#endif /* __linux__ */

void createPidFile(void)
{
    /* If pidfile requested, but no pidfile defined, use
     * default pidfile path */
    if (!server.pidfile)
        server.pidfile = zstrdup(CONFIG_DEFAULT_PID_FILE);

    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(server.pidfile, "w");
    if (fp)
    {
        fprintf(fp, "%d\n", (int)getpid());
        fclose(fp);
    }
}

void daemonize(void)
{
    int fd;

    if (fork() != 0)
        exit(0); /* parent exits 父进程退出*/
    setsid();    /* create a new session */

    /* Every output goes to /dev/null. If Redis is daemonized but
     * the 'logfile' is set to 'stdout' in the configuration file
     * it will not log at all.
     * 如果redis开启了后台模式但是logfile路径设置成控制台输出，那么每一个输出都会输出到/dev/null.*/
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1)
    {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }
}

void version(void)
{
    printf("Redis server v=%s sha=%s:%d malloc=%s bits=%d build=%llx\n",
           REDIS_VERSION,
           redisGitSHA1(),
           atoi(redisGitDirty()) > 0,
           ZMALLOC_LIB,
           sizeof(long) == 4 ? 32 : 64,
           (unsigned long long)redisBuildId());
    exit(0);
}

void usage(void)
{
    fprintf(stderr, "Usage: ./redis-server [/path/to/redis.conf] [options]\n");
    fprintf(stderr, "       ./redis-server - (read config from stdin)\n");
    fprintf(stderr, "       ./redis-server -v or --version\n");
    fprintf(stderr, "       ./redis-server -h or --help\n");
    fprintf(stderr, "       ./redis-server --test-memory <megabytes>\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "       ./redis-server (run the server with default conf)\n");
    fprintf(stderr, "       ./redis-server /etc/redis/6379.conf\n");
    fprintf(stderr, "       ./redis-server --port 7777\n");
    fprintf(stderr, "       ./redis-server --port 7777 --replicaof 127.0.0.1 8888\n");
    fprintf(stderr, "       ./redis-server /etc/myredis.conf --loglevel verbose\n\n");
    fprintf(stderr, "Sentinel mode:\n");
    fprintf(stderr, "       ./redis-server /etc/sentinel.conf --sentinel\n");
    exit(1);
}

void redisAsciiArt(void)
{
#include "asciilogo.h"
    char *buf = zmalloc(1024 * 16);
    char *mode;

    if (server.cluster_enabled)
        mode = "cluster";
    else if (server.sentinel_mode)
        mode = "sentinel";
    else
        mode = "standalone";

    /* Show the ASCII logo if: log file is stdout AND stdout is a
     * tty AND syslog logging is disabled. Also show logo if the user
     * forced us to do so via redis.conf. */
    int show_logo = ((!server.syslog_enabled &&
                      server.logfile[0] == '\0' &&
                      isatty(fileno(stdout))) ||
                     server.always_show_logo);

    if (!show_logo)
    {
        serverLog(LL_NOTICE,
                  "Running mode=%s, port=%d.",
                  mode, server.port ? server.port : server.tls_port);
    }
    else
    {
        snprintf(buf, 1024 * 16, ascii_logo,
                 REDIS_VERSION,
                 redisGitSHA1(),
                 strtol(redisGitDirty(), NULL, 10) > 0,
                 (sizeof(long) == 8) ? "64" : "32",
                 mode, server.port ? server.port : server.tls_port,
                 (long)getpid());
        serverLogRaw(LL_NOTICE | LL_RAW, buf);
    }
    zfree(buf);
}

static void sigShutdownHandler(int sig)
{
    char *msg;

    switch (sig)
    {
    case SIGINT:
        msg = "Received SIGINT scheduling shutdown...";
        break;
    case SIGTERM:
        msg = "Received SIGTERM scheduling shutdown...";
        break;
    default:
        msg = "Received shutdown signal, scheduling shutdown...";
    };

    /* SIGINT is often delivered via Ctrl+C in an interactive session.
     * If we receive the signal the second time, we interpret this as
     * the user really wanting to quit ASAP without waiting to persist
     * on disk. */
    if (server.shutdown_asap && sig == SIGINT)
    {
        serverLogFromHandler(LL_WARNING, "You insist... exiting now.");
        rdbRemoveTempFile(getpid(), 1);
        exit(1); /* Exit with an error since this was not a clean shutdown. */
    }
    else if (server.loading)
    {
        serverLogFromHandler(LL_WARNING, "Received shutdown signal during loading, exiting now.");
        exit(0);
    }

    serverLogFromHandler(LL_WARNING, msg);
    server.shutdown_asap = 1;
}

void setupSignalHandlers(void)
{
    struct sigaction act;

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
     * Otherwise, sa_handler is used. */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigShutdownHandler;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

#ifdef HAVE_BACKTRACE
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
    act.sa_sigaction = sigsegvHandler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
#endif
    return;
}

/* This is the signal handler for children process. It is currently useful
 * in order to track the SIGUSR1, that we send to a child in order to terminate
 * it in a clean way, without the parent detecting an error and stop
 * accepting writes because of a write error condition. */
static void sigKillChildHandler(int sig)
{
    UNUSED(sig);
    int level = server.in_fork_child == CHILD_TYPE_MODULE ? LL_VERBOSE : LL_WARNING;
    serverLogFromHandler(level, "Received SIGUSR1 in child, exiting now.");
    exitFromChild(SERVER_CHILD_NOERROR_RETVAL);
}

void setupChildSignalHandlers(void)
{
    struct sigaction act;

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
     * Otherwise, sa_handler is used. */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigKillChildHandler;
    sigaction(SIGUSR1, &act, NULL);
    return;
}

/* After fork, the child process will inherit the resources
 * of the parent process, e.g. fd(socket or flock) etc.
 * should close the resources not used by the child process, so that if the
 * parent restarts it can bind/lock despite the child possibly still running. */
void closeClildUnusedResourceAfterFork()
{
    closeListeningSockets(0);
    if (server.cluster_enabled && server.cluster_config_file_lock_fd != -1)
        close(server.cluster_config_file_lock_fd); /* don't care if this fails */

    /* Clear server.pidfile, this is the parent pidfile which should not
     * be touched (or deleted) by the child (on exit / crash) */
    zfree(server.pidfile);
    server.pidfile = NULL;
}

/* purpose is one of CHILD_TYPE_ types */
int redisFork(int purpose)
{
    int childpid;
    long long start = ustime();
    if ((childpid = fork()) == 0)
    {
        /* Child */
        server.in_fork_child = purpose;
        setOOMScoreAdj(CONFIG_OOM_BGCHILD);
        setupChildSignalHandlers();
        closeClildUnusedResourceAfterFork();
    }
    else
    {
        /* Parent */
        server.stat_fork_time = ustime() - start;
        server.stat_fork_rate = (double)zmalloc_used_memory() * 1000000 / server.stat_fork_time / (1024 * 1024 * 1024); /* GB per second. */
        latencyAddSampleIfNeeded("fork", server.stat_fork_time / 1000);
        if (childpid == -1)
        {
            return -1;
        }
    }
    return childpid;
}

void sendChildCOWInfo(int ptype, char *pname)
{
    size_t private_dirty = zmalloc_get_private_dirty(-1);

    if (private_dirty)
    {
        serverLog(LL_NOTICE,
                  "%s: %zu MB of memory used by copy-on-write",
                  pname, private_dirty / (1024 * 1024));
    }

    server.child_info_data.cow_size = private_dirty;
    sendChildInfo(ptype);
}

void memtest(size_t megabytes, int passes);

/* Returns 1 if there is --sentinel among the arguments or if
 * argv[0] contains "redis-sentinel". 如果参数中有 --sentinel 或 argv[0] 包含“redis-sentinel”，则返回 1。 */
int checkForSentinelMode(int argc, char **argv)
{
    int j;

    if (strstr(argv[0], "redis-sentinel") != NULL)
        return 1;
    for (j = 1; j < argc; j++)
        if (!strcmp(argv[j], "--sentinel"))
            return 1;
    return 0;
}

/* Function called at startup to load RDB or AOF file in memory. 启动时调用的函数，用于在内存中加载 RDB 或 AOF 文件。*/
void loadDataFromDisk(void)
{
    long long start = ustime();
    if (server.aof_state == AOF_ON)
    {
        if (loadAppendOnlyFile(server.aof_filename) == C_OK)
            serverLog(LL_NOTICE, "DB loaded from append only file: %.3f seconds", (float)(ustime() - start) / 1000000);
    }
    else
    {
        rdbSaveInfo rsi = RDB_SAVE_INFO_INIT;
        errno = 0; /* Prevent a stale value from affecting error checking */
        if (rdbLoad(server.rdb_filename, &rsi, RDBFLAGS_NONE) == C_OK)
        {
            serverLog(LL_NOTICE, "DB loaded from disk: %.3f seconds",
                      (float)(ustime() - start) / 1000000);

            /* Restore the replication ID / offset from the RDB file. */
            if ((server.masterhost ||
                 (server.cluster_enabled &&
                  nodeIsSlave(server.cluster->myself))) &&
                rsi.repl_id_is_set &&
                rsi.repl_offset != -1 &&
                /* Note that older implementations may save a repl_stream_db
                 * of -1 inside the RDB file in a wrong way, see more
                 * information in function rdbPopulateSaveInfo. */
                rsi.repl_stream_db != -1)
            {
                memcpy(server.replid, rsi.repl_id, sizeof(server.replid));
                server.master_repl_offset = rsi.repl_offset;
                /* If we are a slave, create a cached master from this
                 * information, in order to allow partial resynchronizations
                 * with masters. */
                replicationCacheMasterUsingMyself();
                selectDb(server.cached_master, rsi.repl_stream_db);
            }
        }
        else if (errno != ENOENT)
        {
            serverLog(LL_WARNING, "Fatal error loading the DB: %s. Exiting.", strerror(errno));
            exit(1);
        }
    }
}

void redisOutOfMemoryHandler(size_t allocation_size)
{
    serverLog(LL_WARNING, "Out Of Memory allocating %zu bytes!",
              allocation_size);
    serverPanic("Redis aborting for OUT OF MEMORY. Allocating %zu bytes!",
                allocation_size);
}

void redisSetProcTitle(char *title)
{
#ifdef USE_SETPROCTITLE
    char *server_mode = "";
    if (server.cluster_enabled)
        server_mode = " [cluster]";
    else if (server.sentinel_mode)
        server_mode = " [sentinel]";

    setproctitle("%s %s:%d%s",
                 title,
                 server.bindaddr_count ? server.bindaddr[0] : "*",
                 server.port ? server.port : server.tls_port,
                 server_mode);
#else
    UNUSED(title);
#endif
}

void redisSetCpuAffinity(const char *cpulist)
{
#ifdef USE_SETCPUAFFINITY
    setcpuaffinity(cpulist);
#else
    UNUSED(cpulist);
#endif
}

/*
 * Check whether systemd or upstart have been used to start redis.
 */

int redisSupervisedUpstart(void)
{
    const char *upstart_job = getenv("UPSTART_JOB");

    if (!upstart_job)
    {
        serverLog(LL_WARNING,
                  "upstart supervision requested, but UPSTART_JOB not found");
        return 0;
    }

    serverLog(LL_NOTICE, "supervised by upstart, will stop to signal readiness");
    raise(SIGSTOP);
    unsetenv("UPSTART_JOB");
    return 1;
}

int redisCommunicateSystemd(const char *sd_notify_msg)
{
    const char *notify_socket = getenv("NOTIFY_SOCKET");
    if (!notify_socket)
    {
        serverLog(LL_WARNING,
                  "systemd supervision requested, but NOTIFY_SOCKET not found");
    }

#ifdef HAVE_LIBSYSTEMD
    (void)sd_notify(0, sd_notify_msg);
#else
    UNUSED(sd_notify_msg);
#endif
    return 0;
}

int redisIsSupervised(int mode)
{
    if (mode == SUPERVISED_AUTODETECT)
    {
        const char *upstart_job = getenv("UPSTART_JOB");
        const char *notify_socket = getenv("NOTIFY_SOCKET");

        if (upstart_job)
        {
            redisSupervisedUpstart();
        }
        else if (notify_socket)
        {
            server.supervised_mode = SUPERVISED_SYSTEMD;
            serverLog(LL_WARNING,
                      "WARNING auto-supervised by systemd - you MUST set appropriate values for TimeoutStartSec and TimeoutStopSec in your service unit.");
            return redisCommunicateSystemd("STATUS=Redis is loading...\n");
        }
    }
    else if (mode == SUPERVISED_UPSTART)
    {
        return redisSupervisedUpstart();
    }
    else if (mode == SUPERVISED_SYSTEMD)
    {
        serverLog(LL_WARNING,
                  "WARNING supervised by systemd - you MUST set appropriate values for TimeoutStartSec and TimeoutStopSec in your service unit.");
        return redisCommunicateSystemd("STATUS=Redis is loading...\n");
    }

    return 0;
}

int iAmMaster(void)
{
    return ((!server.cluster_enabled && server.masterhost == NULL) ||
            (server.cluster_enabled && nodeIsMaster(server.cluster->myself)));
}

/*主程序入口*/
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
    // process_class值为-1表示OOM_CONFIG_MASTER或OOM_CONFIG_REPLICA，具体取决于当前角色。
    setOOMScoreAdj(-1);

    //开启主事件循环，处理数据
    aeMain(server.el);
    //释放所有事件循环
    aeDeleteEventLoop(server.el);
    return 0;
}

/* The End */
