/*
 * path_planner.h
 *
 *  Created on: Sep 7, 2017
 *      Author: ramiz
 */

#ifndef PATH_PLANNER_H_
#define PATH_PLANNER_H_

#include <iostream>
#include <vector>
#include "vehicle.h"
#include "trajectory_generator.h"
#include "trajectory.h"

using namespace std;

class PathPlanner {
public:
  PathPlanner();

  virtual ~PathPlanner();

  // Sensor Fusion Data, a list of all other cars on the same side of the road.
  //The data format for each car is: [ id, x, y, vx, vy, s, d]
  Trajectory GenerateTrajectory(const Vehicle &ego_vehicle,
                                const vector<vector<double> > &sensor_fusion_data,
                                const vector<double> &previous_path_x,
                                const vector<double> &previous_path_y,
                                const double previous_path_last_s,
                                const double previous_path_last_d);

private:
  vector<Vehicle> ExtractSensorFusionData(const vector<vector<double> > &sensor_fusion_data, const int previous_path_size);
  void UpdateEgoVehicleStateWithRespectToPreviousPath(const vector<double> &previous_path_x,
                                                      const vector<double> &previous_path_y,
                                                      const double previous_path_last_s,
                                                      const double previous_path_last_d);
  double FindDistanceFromVehicleAhead();
  bool IsTooCloseToVehicleAhead();

  vector<Vehicle> vehicles_;
  Vehicle ego_vehicle_;
  int lane_;
  double reference_velocity_;

  const double BUFFER_DISTANCE = 10;
  const double SPEED_LIMIT = 49.5;
  // The max s value before wrapping around the track back to 0
  const double MAX_S = 6945.554;
};

#endif /* PATH_PLANNER_H_ */