/**
 * @file trajectory_solver_solve_iter_test.cpp
 * @brief Card 0-8 RED: 验证 Solve 迭代预测时序与 SP 对齐
 */

#include "modules/ekf_predictor/detail/ekf_track_target.hpp"
#include "modules/ekf_predictor/detail/trajectory_solver.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

#include <Eigen/Dense>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

struct TrajectoryResult {
  bool unsolvable{true};
  double fly_time{0.0};
  double pitch{0.0};
};

struct TrajectoryInput {
  double horizontal_distance{0.0};
  double vertical_height{0.0};
};

struct DetectionInput {
  double position_x{0.0};
  double position_y{0.0};
  double position_z{0.0};
  double yaw_angle{0.0};
};

TrajectoryResult SolveTrajectoryForTest(double bullet_speed, const TrajectoryInput& input) {
  double gravity_constant = 9.7833;
  if (bullet_speed < 1.0 || input.horizontal_distance < 1e-3) {
    return {};
  }

  double coeff_a = gravity_constant * input.horizontal_distance * input.horizontal_distance /
                   (2.0 * bullet_speed * bullet_speed);
  double coeff_b = -input.horizontal_distance;
  double coeff_c = coeff_a + input.vertical_height;
  double delta = coeff_b * coeff_b - 4.0 * coeff_a * coeff_c;
  if (delta < 0.0) {
    return {};
  }

  double tan_1 = (-coeff_b + std::sqrt(delta)) / (2.0 * coeff_a);
  double tan_2 = (-coeff_b - std::sqrt(delta)) / (2.0 * coeff_a);
  double pitch_1 = std::atan(tan_1);
  double pitch_2 = std::atan(tan_2);
  double fly_time_1 = input.horizontal_distance / (bullet_speed * std::cos(pitch_1));
  double fly_time_2 = input.horizontal_distance / (bullet_speed * std::cos(pitch_2));

  bool use_first = fly_time_1 < fly_time_2;
  return {false, use_first ? fly_time_1 : fly_time_2, use_first ? pitch_1 : pitch_2};
}

mv::Detection MakeDetection(const DetectionInput& input) {
  mv::Detection det;
  det.is_solved = true;
  det.color = mv::ArmorColor::RED;
  det.number = mv::ArmorNumber::THREE;
  det.type = mv::ArmorType::SMALL;
  det.xyz_in_gimbal = Eigen::Vector3d(input.position_x, input.position_y, input.position_z);
  det.yaw_angle = input.yaw_angle;
  det.distance_to_center = 0.5;
  return det;
}

}  // namespace

int main() {
  using mv::modules::detail::EkfTrackTarget;
  using mv::modules::detail::ProcessNoiseParams;
  using mv::modules::detail::TrajectorySolver;
  using mv::modules::detail::TrajectorySolverParams;

  auto time_point_0 = std::chrono::steady_clock::now();
  Eigen::VectorXd p0_diag(11);
  p0_diag << 1, 64, 1, 64, 1, 64, 0.4, 100, 1, 1, 1;

  EkfTrackTarget target(MakeDetection({4.0, 0.0, 0.3, 0.0}), time_point_0, 0.27, 4, p0_diag,
                        ProcessNoiseParams{});
  target.jumped = false;  // 固定选择 id=0，便于构造可验证基线

  // 注入变化观测，制造可见动态（中心/角度都发生变化）。
  target.Update(MakeDetection({4.1, 0.2, 0.35, 0.12}));
  target.Update(MakeDetection({4.2, 0.35, 0.4, 0.25}));
  target.jumped = false;  // 测试中仍固定 id0 选板

  TrajectorySolverParams solver_params;
  solver_params.max_iter = 1;          // 仅验证第一轮时序
  solver_params.iter_converge_ms = 0;  // 不触发收敛提前退出，执行完整首轮
  solver_params.max_approaching_angle = 2.0;
  solver_params.pitch_offset_rad = 0.02;
  solver_params.low_speed_delay_ms = 100.0;
  solver_params.high_speed_delay_ms = 70.0;
  solver_params.decision_speed = 25.0;
  TrajectorySolver solver(solver_params);

  auto ctrl = solver.Solve(target, time_point_0, 23.0);
  if (!AssertOrFail(ctrl.tracking, "solve should output tracking=true")) {
    return 1;
  }
  if (!AssertOrFail(!ctrl.fire, "solve should keep fire=false (voter boundary)")) {
    return 1;
  }

  auto debug_aim = solver.GetDebugAimPoint();
  if (!AssertOrFail(debug_aim.valid, "debug aim point should be valid")) {
    return 1;
  }

  // 构造 SP 口径基线：
  // 1) 先预测到 future（init delay）
  // 2) 用 future 状态估计 fly_time0
  // 3) 迭代时从 future 再预测到 future + fly_time0（而非从原始 t0 直接预测）
  double delay_seconds = solver_params.low_speed_delay_ms / 1000.0;
  double init_dt_seconds = 0.005 + delay_seconds;
  auto future_time =
      time_point_0 + std::chrono::microseconds(static_cast<int64_t>(init_dt_seconds * 1e6));

  EkfTrackTarget predicted_target = target;
  predicted_target.Predict(future_time);
  auto first_xyza = predicted_target.ArmorXyzaList().front();
  Eigen::Vector3d first_xyz = first_xyza.head<3>();
  auto traj_0 =
      SolveTrajectoryForTest(23.0, {std::hypot(first_xyz[0], first_xyz[1]), first_xyz[2]});
  if (!AssertOrFail(!traj_0.unsolvable, "traj_0 should be solvable")) {
    return 1;
  }

  auto predict_time =
      future_time + std::chrono::microseconds(static_cast<int64_t>(traj_0.fly_time * 1e6));
  EkfTrackTarget iter_target = predicted_target;
  iter_target.Predict(predict_time);
  auto iter_xyza = iter_target.ArmorXyzaList().front();
  Eigen::Vector3d iter_xyz = iter_xyza.head<3>();
  auto expected_traj =
      SolveTrajectoryForTest(23.0, {std::hypot(iter_xyz[0], iter_xyz[1]), iter_xyz[2]});
  if (!AssertOrFail(!expected_traj.unsolvable, "expected trajectory should be solvable")) {
    return 1;
  }

  double expected_pitch = -(expected_traj.pitch + solver_params.pitch_offset_rad);
  if (!AssertOrFail(std::abs(ctrl.pitch - expected_pitch) < 1e-5,
                    "solve pitch should follow SP-style future-based iteration timing")) {
    std::cerr << "[INFO] ctrl.pitch=" << ctrl.pitch << " expected_pitch=" << expected_pitch << "\n";
    return 1;
  }

  std::cout << "[PASS] trajectory_solver_solve_iter_test\n";
  return 0;
}
