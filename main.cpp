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

struct AnimationData {
  float pre_loop_duration = 0.f;
  float loop_duration = 0.f;
  float post_loop_duration = 0.f;
  float execute0 = 0.f;
  float execute1 = 0.f;
  float evade_duration = 0.f;
};

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
      size_t loop_begin_index = 0;
      size_t loop_end_index = 0;
      for (size_t i = 0; i < anim_data->steps.size(); i++) {
        const auto &step = anim_data->steps[i];
        if (step.flags & 0x10) {
          loop_begin_index = i;
        }
        if (step.flags & 0x20) {
          loop_end_index = i + 1;
        }
      }

      if (loop_begin_index != loop_end_index) {
        auto step_count_a = 0;
        auto step_count_b = 0;
        for (size_t i = 0; i < anim_data->steps.size();) {
          const auto &step = anim_data->steps[i];
          const auto duration =
              (step.type == 0 ? step.action->duration : step.move->duration) /
              1000.f;

          if (step.flags & 0xf) {
            result.evade_duration += duration;
          }

          if (i < loop_begin_index) {
            result.pre_loop_duration += duration;
          }

          if (i >= loop_end_index) {
            result.post_loop_duration += duration;
          }

          bool inside_loop = i >= loop_begin_index && i < loop_end_index;
          if (inside_loop) {
            if (step_count_a < std::get<0>(loop_step_count)) {
              result.loop_duration += duration;
              step_count_a++;
            }
            if (step_count_b < std::get<1>(loop_step_count)) {
              step_count_b++;
            }
          }

          // Skip to end
          if (inside_loop && step_count_b == std::get<1>(loop_step_count)) {
            i = loop_end_index;
          } else {
            if (i == (loop_end_index - 1)) {
              i = loop_begin_index;
            } else {
              i++;
            }
          }
        }
      } else {
        for (const auto &step : anim_data->steps) {
          const auto duration =
              (step.type == 0 ? step.action->duration : step.move->duration) /
              1000.f;
          if (step.flags & 0xf) {
            result.evade_duration += duration;
          }
          result.pre_loop_duration += duration;
        }
      }

      auto time = INFINITY;
      auto time_end = 0.f;
      for (auto &trigger : anim_data->triggers) {
        if (trigger.trigger == 3) {
          time = std::min(time, trigger.time / 1000.f);
          time_end = std::max(time_end, trigger.time / 1000.f);
        }
      }

      auto fixed_loop_duration = 0.f;
      for (size_t i = loop_begin_index; i < loop_end_index; i++) {
        const auto &step = anim_data->steps[i];
        fixed_loop_duration +=
            (step.type == 0 ? step.action->duration : step.move->duration) /
            1000.f;
      }

      if (time <= (fixed_loop_duration + result.pre_loop_duration)) {
        result.execute0 = time;
      } else {
        result.execute0 = (result.loop_duration - fixed_loop_duration) + time;
      }

      if (time_end < result.pre_loop_duration) {
        result.execute1 = time_end;
      } else {
        result.execute1 =
            (result.loop_duration - fixed_loop_duration) + time_end;
      }
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

    std::cout << "pre_loop_duration: " << data.pre_loop_duration << std::endl;
    std::cout << "loop_duration: " << data.loop_duration << std::endl;
    std::cout << "post_loop_duration: " << data.post_loop_duration << std::endl;
    std::cout << "execute0: " << data.execute0 << std::endl;
    std::cout << "execute1: " << data.execute1 << std::endl;
    std::cout << "evade_duration: " << data.evade_duration << std::endl;
  }

  return 0;
}