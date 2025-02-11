/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include <cstdint>

#include "commander.h"
#include "commands/ttl_util.h"
#include "error_constants.h"
#include "server/redis_reply.h"
#include "server/server.h"
#include "storage/redis_db.h"
#include "time_util.h"

namespace redis {

class CommandType : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    RedisType type = kRedisNone;
    auto s = redis.Type(args_[1], &type);
    if (s.ok()) {
      *output = redis::SimpleString(RedisTypeNames[type]);
      return Status::OK();
    }

    return {Status::RedisExecErr, s.ToString()};
  }
};

class CommandMove : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override {
    GET_OR_RET(ParseInt<int64_t>(args[2], 10));
    return Status::OK();
  }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    int count = 0;
    redis::Database redis(svr->storage, conn->GetNamespace());
    rocksdb::Status s = redis.Exists({args_[1]}, &count);
    if (!s.ok()) return {Status::RedisExecErr, s.ToString()};

    *output = count ? redis::Integer(1) : redis::Integer(0);
    return Status::OK();
  }
};

class CommandObject : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    if (util::ToLower(args_[1]) == "dump") {
      redis::Database redis(svr->storage, conn->GetNamespace());
      std::vector<std::string> infos;
      auto s = redis.Dump(args_[2], &infos);
      if (!s.ok()) {
        return {Status::RedisExecErr, s.ToString()};
      }

      output->append(redis::MultiLen(infos.size()));
      for (const auto &info : infos) {
        output->append(redis::BulkString(info));
      }
    } else {
      return {Status::RedisExecErr, "object subcommand must be dump"};
    }
    return Status::OK();
  }
};

class CommandTTL : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    int64_t ttl = 0;
    auto s = redis.TTL(args_[1], &ttl);
    if (s.ok()) {
      *output = redis::Integer(ttl > 0 ? ttl / 1000 : ttl);
      return Status::OK();
    }

    return {Status::RedisExecErr, s.ToString()};
  }
};

class CommandPTTL : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    int64_t ttl = 0;
    auto s = redis.TTL(args_[1], &ttl);
    if (!s.ok()) return {Status::RedisExecErr, s.ToString()};

    *output = redis::Integer(ttl);
    return Status::OK();
  }
};

class CommandExists : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    std::vector<rocksdb::Slice> keys;
    keys.reserve(args_.size() - 1);
    for (size_t i = 1; i < args_.size(); i++) {
      keys.emplace_back(args_[i]);
    }

    int cnt = 0;
    redis::Database redis(svr->storage, conn->GetNamespace());
    redis.Exists(keys, &cnt);
    *output = redis::Integer(cnt);

    return Status::OK();
  }
};

class CommandExpire : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override {
    ttl_ = GET_OR_RET(ParseInt<int64_t>(args[2], 10));
    return Status::OK();
  }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    auto s = redis.Expire(args_[1], ttl_ * 1000 + util::GetTimeStampMS());
    if (s.ok()) {
      *output = redis::Integer(1);
    } else {
      *output = redis::Integer(0);
    }
    return Status::OK();
  }

 private:
  uint64_t ttl_ = 0;
};

class CommandPExpire : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override {
    seconds_ = GET_OR_RET(ParseInt<int64_t>(args[2], 10)) + util::GetTimeStampMS();
    return Status::OK();
  }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    auto s = redis.Expire(args_[1], seconds_);
    if (s.ok()) {
      *output = redis::Integer(1);
    } else {
      *output = redis::Integer(0);
    }
    return Status::OK();
  }

 private:
  uint64_t seconds_ = 0;
};

class CommandExpireAt : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override {
    auto parse_result = ParseInt<int64_t>(args[2], 10);
    if (!parse_result) {
      return {Status::RedisParseErr, errValueNotInteger};
    }

    timestamp_ = *parse_result;

    return Commander::Parse(args);
  }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    auto s = redis.Expire(args_[1], timestamp_ * 1000);
    if (s.ok()) {
      *output = redis::Integer(1);
    } else {
      *output = redis::Integer(0);
    }
    return Status::OK();
  }

 private:
  uint64_t timestamp_ = 0;
};

class CommandPExpireAt : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override {
    auto parse_result = ParseInt<int64_t>(args[2], 10);
    if (!parse_result) {
      return {Status::RedisParseErr, errValueNotInteger};
    }

    timestamp_ = *parse_result;

    return Commander::Parse(args);
  }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    redis::Database redis(svr->storage, conn->GetNamespace());
    auto s = redis.Expire(args_[1], timestamp_);
    if (s.ok()) {
      *output = redis::Integer(1);
    } else {
      *output = redis::Integer(0);
    }
    return Status::OK();
  }

 private:
  uint64_t timestamp_ = 0;
};

class CommandPersist : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    int64_t ttl = 0;
    redis::Database redis(svr->storage, conn->GetNamespace());
    auto s = redis.TTL(args_[1], &ttl);
    if (!s.ok()) return {Status::RedisExecErr, s.ToString()};

    if (ttl == -1 || ttl == -2) {
      *output = redis::Integer(0);
      return Status::OK();
    }

    s = redis.Expire(args_[1], 0);
    if (!s.ok()) return {Status::RedisExecErr, s.ToString()};

    *output = redis::Integer(1);
    return Status::OK();
  }
};

class CommandDel : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    std::vector<rocksdb::Slice> keys;
    keys.reserve(args_.size() - 1);
    for (size_t i = 1; i < args_.size(); i++) {
      keys.emplace_back(args_[i]);
    }

    uint64_t cnt = 0;
    redis::Database redis(svr->storage, conn->GetNamespace());
    auto s = redis.MDel(keys, &cnt);
    if (!s.ok()) return {Status::RedisExecErr, s.ToString()};

    *output = redis::Integer(cnt);
    return Status::OK();
  }
};

REDIS_REGISTER_COMMANDS(MakeCmdAttr<CommandTTL>("ttl", 2, "read-only", 1, 1, 1),
                        MakeCmdAttr<CommandPTTL>("pttl", 2, "read-only", 1, 1, 1),
                        MakeCmdAttr<CommandType>("type", 2, "read-only", 1, 1, 1),
                        MakeCmdAttr<CommandMove>("move", 3, "write", 1, 1, 1),
                        MakeCmdAttr<CommandObject>("object", 3, "read-only", 2, 2, 1),
                        MakeCmdAttr<CommandExists>("exists", -2, "read-only", 1, -1, 1),
                        MakeCmdAttr<CommandPersist>("persist", 2, "write", 1, 1, 1),
                        MakeCmdAttr<CommandExpire>("expire", 3, "write", 1, 1, 1),
                        MakeCmdAttr<CommandPExpire>("pexpire", 3, "write", 1, 1, 1),
                        MakeCmdAttr<CommandExpireAt>("expireat", 3, "write", 1, 1, 1),
                        MakeCmdAttr<CommandPExpireAt>("pexpireat", 3, "write", 1, 1, 1),
                        MakeCmdAttr<CommandDel>("del", -2, "write", 1, -1, 1),
                        MakeCmdAttr<CommandDel>("unlink", -2, "write", 1, -1, 1), )

}  // namespace redis
