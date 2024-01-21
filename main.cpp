#include <assert.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <pf/formats/anic.hpp>
#include <string>
#include <utility>
#include <variant>

template <typename T>
concept Chunk = !
std::is_same_v<std::decay_t<T>, std::monostate>;

struct Trigger {
  uint32_t type;
  uint32_t flags;
  uint32_t time;
};

struct AnimationData {
  uint32_t total_duration;
  uint32_t pre_loop_duration;
  uint32_t loop_duration;
  uint32_t post_loop_duration;
  uint32_t execute0;
  uint32_t execute1;
  uint32_t evade_duration;
  std::vector<Trigger> triggers;
  uint32_t flags;
};

/*
Extreme:
Fly:
Hop:
Sidestep:
*/

constexpr auto SequenceStepFlagsEvadeExtreme = (1 << 0);
constexpr auto SequenceStepFlagsEvadeFly = (1 << 1);
constexpr auto SequenceStepFlagsEvadeHop = (1 << 2);
constexpr auto SequenceStepFlagsEvadeSidestep = (1 << 3);
constexpr auto SequenceStepFlagsLoopBegin = (1 << 4);
constexpr auto SequenceStepFlagsLoopEnd = (1 << 5);

constexpr auto SequenceStepFlagsEvadeAll =
    SequenceStepFlagsEvadeExtreme | SequenceStepFlagsEvadeFly |
    SequenceStepFlagsEvadeHop | SequenceStepFlagsEvadeSidestep;

[[nodiscard]] AnimationData GetAnimationData(
    const Chunk auto chunk, uint64_t animation, uint64_t variant,
    std::pair<uint32_t, uint32_t> loop_step_count) {
  AnimationData result{};
  const auto &sequence =
      std::find_if(chunk.sequences.begin(), chunk.sequences.end(),
                   [animation](const auto &sequence) {
                     return sequence.sequence == animation;
                   });
  if (sequence != chunk.sequences.end()) {
    const auto &anim_data = std::find_if(sequence->animation_data.begin(),
                                         sequence->animation_data.end(),
                                         [variant](const auto &anim_data) {
                                           return anim_data.token == variant;
                                         });
    if (anim_data != sequence->animation_data.end()) {
      // If the animation has a loop try find the beginning and the end
      size_t loop_begin_index{};
      size_t loop_end_index{};
      for (size_t loop_index{}; loop_index < anim_data->steps.size();
           loop_index++) {
        const auto &step = anim_data->steps[loop_index];
        if (step.flags & SequenceStepFlagsLoopBegin) {
          loop_begin_index = loop_index;
        }
        // A single step can be both, the beginning and the end of a loop
        if (step.flags & SequenceStepFlagsLoopEnd) {
          loop_end_index = loop_index + 1;
        }
      }

      assert(loop_end_index >= loop_begin_index);
      const bool has_loop = loop_begin_index != loop_end_index;

      // Calculate how long a single loop iteration takes
      uint32_t loop_single_duration{};
      if (has_loop) {
        for (size_t i = loop_begin_index; i < loop_end_index; i++) {
          const auto &step = anim_data->steps[i];
          loop_single_duration +=
              step.type == 0 ? step.action->duration : step.move->duration;
        }
      }

      size_t trigger_index{};
      size_t trigger_begin_loop_index{};
      uint32_t step_count_a{};
      uint32_t step_count_b{};

      for (size_t i = 0u; i < anim_data->steps.size();) {
        const auto inside_loop =
            has_loop ? i >= loop_begin_index && i < loop_end_index : false;

        const auto &step = anim_data->steps[i];
        const auto step_duration =
            step.type == 0 ? step.action->duration : step.move->duration;
        result.total_duration += step_duration;

        if (step.flags & SequenceStepFlagsEvadeAll) {
          result.evade_duration += step_duration;
        }

        if (i < loop_begin_index) {
          result.pre_loop_duration += step_duration;
        }

        if (i >= loop_end_index) {
          result.post_loop_duration += step_duration;
        }

        for (; trigger_index < anim_data->triggers.size();) {
          const auto &trigger = anim_data->triggers[trigger_index];

          auto time = trigger.time;
          if (has_loop) {
            // Adjust time if inside or after a loop
            if (i >= loop_end_index) {
              time += result.loop_duration - loop_single_duration;
            } else if (inside_loop) {
              time += (result.loop_duration / loop_single_duration) *
                      loop_single_duration;
            }
          }

          if (time > result.total_duration) {
            break;
          }

          result.triggers.emplace_back(trigger.trigger, trigger.flags, time);
          trigger_index++;
        }

        if (inside_loop) {
          if (step_count_a < std::get<0>(loop_step_count)) {
            result.loop_duration += step_duration;
            step_count_a++;
          }
          if (step_count_b < std::get<1>(loop_step_count)) {
            step_count_b++;
          }
        }

        if (inside_loop && step_count_b == std::get<1>(loop_step_count)) {
          i = loop_end_index;
        } else {
          if (i == (loop_end_index - 1)) {
            i = loop_begin_index;
            trigger_index = trigger_begin_loop_index;
          } else {
            i++;
            if (i == loop_begin_index) {
              trigger_begin_loop_index = trigger_index;
            }
          }
        }
      }

      auto time_first_trigger = std::numeric_limits<uint32_t>::max();
      auto time_last_trigger = std::numeric_limits<uint32_t>::max();
      for (auto &trigger : anim_data->triggers) {
        if (trigger.trigger == 3) {
          if (time_first_trigger == std::numeric_limits<uint32_t>::max() ||
              time_first_trigger > trigger.time) {
            time_first_trigger = trigger.time;
          }
          if (time_last_trigger == std::numeric_limits<uint32_t>::max() ||
              trigger.time > time_last_trigger) {
            time_last_trigger = trigger.time;
          }
        }
      }

      uint32_t fixed_loop_duration{};
      for (size_t i = loop_begin_index; i < loop_end_index; i++) {
        const auto &step = anim_data->steps[i];
        fixed_loop_duration +=
            step.type == 0 ? step.action->duration : step.move->duration;
      }

      if (time_first_trigger <=
          (fixed_loop_duration + result.pre_loop_duration)) {
        result.execute0 = time_first_trigger;
      } else {
        result.execute0 =
            (result.loop_duration - fixed_loop_duration) + time_first_trigger;
      }

      if (time_last_trigger < result.pre_loop_duration) {
        result.execute1 = time_last_trigger;
      } else {
        result.execute1 =
            (result.loop_duration - fixed_loop_duration) + time_last_trigger;
      }

      result.flags = anim_data->flags;
    }
  }

  return result;
}

[[nodiscard]] AnimationData GetAnimationData(std::monostate, uint64_t, uint64_t,
                                             std::pair<uint32_t, uint32_t>) {
  return {};
}

[[nodiscard]] std::optional<pf::anic::seqn::PackAnimSequences> Load(
    const std::filesystem::path &file_path) {
  if (!std::filesystem::exists(file_path)) {
    return std::nullopt;
  }

  std::basic_ifstream<std::byte> stream(file_path,
                                        std::ios::in | std::ios::binary);
  auto size = std::filesystem::file_size(file_path);
  std::vector<std::byte> buf{size};
  stream.read(buf.data(), size);

  if (auto pf = pf::AnicReader::From(buf)) {
    if (auto chunk = pf->Chunk<pf::FourCC::seqn>()) {
      return *chunk;
    }
  }

  return std::nullopt;
}

int main(int argc, char *argv[]) {
  const auto printBlurb = []() {
    std::cout << "animation variant? loop_step_count.0? loop_step_count.1?"
              << std::endl;
  };

  if (argc <= 1) {
    std::cerr << "invalid argc" << std::endl;
    printBlurb();
    return -1;
  }

  uint64_t animation = std::stoull(argv[1]);
  uint64_t variant = 0;
  if (argc > 2) {
    variant = std::stoull(argv[2]);
  }
  std::pair<uint32_t, uint32_t> loop_step_count = {0, 0};
  if (argc > 3) {
    std::get<0>(loop_step_count) = std::stoul(argv[3]);
  }
  if (argc > 4) {
    std::get<1>(loop_step_count) = std::stoul(argv[4]);
  }

  if (const auto chunk = Load("184788")) {
    auto data = std::visit(
        [=](const auto &seqn) {
          return GetAnimationData(seqn, animation, variant, loop_step_count);
        },
        *chunk);

    std::cout << "total_duration: " << data.total_duration << std::endl;
    std::cout << "pre_loop_duration: " << data.pre_loop_duration << std::endl;
    std::cout << "loop_duration: " << data.loop_duration << std::endl;
    std::cout << "post_loop_duration: " << data.post_loop_duration << std::endl;
    std::cout << "execute0: " << data.execute0 << std::endl;
    std::cout << "execute1: " << data.execute1 << std::endl;
    std::cout << "evade_duration: " << data.evade_duration << std::endl;
    std::cout << "triggers:" << std::endl;
    for (const auto &trigger : data.triggers) {
      std::cout << '\t' << "type: " << trigger.type
                << " flags: " << trigger.flags << " time: " << trigger.time
                << std::endl;
    }
    std::cout << "flags:" << data.flags << std::endl;
  }

  return 0;
}