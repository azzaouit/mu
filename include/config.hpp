#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <openssl/sha.h>

#define MU_LOG(MSG) (std::cerr << "[mu] " << MSG << std::endl)
#define MU_LOG_ERR(MSG)                                                        \
  (std::cerr << __FILE__ << ":" << __LINE__ << " " << MSG << std::endl)

/* RDMA constants */
#define MU_MAX_WR (1 << 4)
#define MU_MAX_SGE (1 << 4)
#define MU_CQ_CAPACITY (1 << 4)
#define MU_SEND_WRID (1 << 0)
#define MU_RECV_WRID (1 << 1)

struct Config {
  struct in_addr ip;
  uint16_t id;
  uint16_t port;
  uint16_t ib_port;
  uint16_t gid_index;
  Config() = default;
  friend std::ostream &operator<<(std::ostream &os, const Config &c) {
    char *ipstr = inet_ntoa(c.ip);
    os << "[" << c.id << "] " << ipstr << ":" << c.port << " " << c.ib_port
       << " " << c.gid_index;
    return os;
  }
  friend std::ifstream &operator>>(std::ifstream &fs, Config &c) {
    int val;
    std::stringstream s;
    std::string line, key;
    for (int j = 0; j < 4; ++j) {
      if (std::getline(fs, line)) {
        std::istringstream iss(line);
        if (!(iss >> key >> val)) {
          MU_LOG_ERR("Invalid configuration file");
          return fs;
        }
        if (key == "id")
          c.id = val;
        else if (key == "port")
          c.port = val;
        else if (key == "ibport")
          c.ib_port = val;
        else if (key == "gididx")
          c.gid_index = val;
      } else if (fs.eof()) {
        MU_LOG_ERR("Configuration file EOF");
        return fs;
      } else {
        MU_LOG_ERR("Error reading line");
        return fs;
      }
    }
    return fs;
  }
} __attribute__((packed));

struct PeerConfig {
  Config config;
  std::string hash;
  friend std::ostream &operator<<(std::ostream &os, const PeerConfig &p) {
    os << p.config << " " << p.hash;
    return os;
  }
  friend std::ifstream &operator>>(std::ifstream &fs, PeerConfig &p) {
    std::stringstream ss;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    fs >> p.config;

    SHA256((unsigned char *)&p.config, sizeof(p.config), hash);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    p.hash = ss.str();

    return fs;
  }
};

struct NetConfig {
  PeerConfig host;
  std::vector<PeerConfig> peers;
  friend std::ostream &operator<<(std::ostream &os, const NetConfig &n) {
    os << "Host" << std::endl << n.host << std::endl;
    os << "Peers" << std::endl;
    for (auto &p : n.peers)
      os << p << std::endl;
    return os;
  }
  friend std::ifstream &operator>>(std::ifstream &fs, NetConfig &n) {
    struct in_addr ip;
    std::string line, key, addr;
    while (std::getline(fs, line)) {
      std::istringstream iss(line);
      if (!(iss >> key >> addr))
        continue;
      if (key == "host") {
        if (!inet_aton(addr.c_str(), &ip)) {
          MU_LOG_ERR("Invalid host IPV4 address");
          return fs;
        }
        n.host.config.ip.s_addr = ip.s_addr;
        fs >> n.host;
      } else if (key == "peer") {
        PeerConfig p;
        if (!inet_aton(addr.c_str(), &ip)) {
          MU_LOG_ERR("Invalid host IPV4 address");
          return fs;
        }
        p.config.ip.s_addr = ip.s_addr;
        fs >> p;
        n.peers.push_back(p);
      } else {
        MU_LOG_ERR("Configuration parse error. Unexpected key " << key);
        return fs;
      }
    }
    return fs;
  }
};

#endif /* CONFIG_HPP */
