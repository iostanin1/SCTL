#ifndef _PVFMM_PROFILE_HPP_
#define _PVFMM_PROFILE_HPP_

#include <string>
#include <vector>
#include <stack>

#include <pvfmm/common.hpp>

#ifndef PVFMM_PROFILE
#define PVFMM_PROFILE -1
#endif

namespace pvfmm {

class Comm;

class Profile {
 public:
  static Long Add_FLOP(Long inc);

  static Long Add_MEM(Long inc);

  static bool Enable(bool state);

  static void Tic(const char* name_, const Comm* comm_ = NULL, bool sync_ = false, Integer level = 0);

  static void Toc();

  static void print(const Comm* comm_ = NULL);

  static void reset();

 private:
  struct ProfileData {
    Long MEM;
    Long FLOP;
    bool enable_state;
    std::stack<bool> sync;
    std::stack<std::string> name;
    std::stack<const Comm*> comm;
    std::vector<Long> max_mem;

    Integer enable_depth;
    std::stack<int> verb_level;

    std::vector<bool> e_log;
    std::vector<bool> s_log;
    std::vector<std::string> n_log;
    std::vector<double> t_log;
    std::vector<Long> f_log;
    std::vector<Long> m_log;
    std::vector<Long> max_m_log;
    ProfileData() {
      FLOP = 0;
      MEM = 0;
      enable_state = false;
      enable_depth = 0;
    }
  };

  static inline ProfileData& ProfData() {
    static ProfileData p;
    return p;
  }
};

}  // end namespace

#include <pvfmm/profile.txx>

#endif  //_PVFMM_PROFILE_HPP_
