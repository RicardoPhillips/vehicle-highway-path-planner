/*
 * path_planner.cpp
 *
 *  Created on: Sep 7, 2017
 *      Author: ramiz
 */

#include "map_utils.h"
#include "path_planner.h"

// Sensor Fusion Data, a list of all other cars on the same side of the road.
//The data format for each car is: [ id, x, y, vx, vy, s, d]
PathPlanner::PathPlanner() {
  this->lane_ = 1;
  this->reference_velocity_ = 0.0;
}

//Sensor Fusion Data, a list of all other cars on the same side of the road.
//The data format for each car is: [ id, x, y, vx, vy, s, d]'
vector<Vehicle> PathPlanner::ExtractSensorFusionData(const vector<vector<double> > &sensor_fusion_data,
                                                     const int previous_path_size) {

  vector<Vehicle> vehicles;

  for (int i = 0; i < sensor_fusion_data.size(); ++i) {
    int id = sensor_fusion_data[i][0];
    double x = sensor_fusion_data[i][1];
    double y = sensor_fusion_data[i][2];

    //access vx, vy which are at indexes (3, 4)
    double vx = sensor_fusion_data[i][3];
    double vy = sensor_fusion_data[i][4];

    //s-coordinate is at index 5
    double s = sensor_fusion_data[i][5];
    //d-coordinate is at index 6
    double d = sensor_fusion_data[i][6];

    //calculate total velocity
    double total_v = sqrt(vx*vx + vy*vy);

    Vehicle vehicle(id, x, y, s, d, 0, total_v, 0);

    //if previous_path size is not 0 that means
    //the Simulator has not yet traversed complete prev path and
    //the vehicle is not in perspective with the
    //new path/trajectory we are going to build so
    //we should predict its s-coordinate like if
    //we previous path was already traversed
    //REMEMBER: 1 timestep = 0.02 secs (or 20ms)
    //    vehicle.increment(0.02 * previous_path_size);

    vehicles.push_back(vehicle);
  }

  return vehicles;
}

void PathPlanner::UpdateEgoVehicleStateWithRespectToPreviousPath() {
  //previous path is not empty that means simulator has
  //not traversed it yet and car is still in somewhere on that path
  //we have to provide new path so start from previous path end
  //hence previous path end point will become the new reference point to start
  int prev_path_size = previous_path_x_.size();

  if (prev_path_size > 2) {
    ego_vehicle_.s = previous_path_last_s_;
    ego_vehicle_.d = previous_path_last_d_;

    ego_vehicle_.x = previous_path_x_[prev_path_size - 1];
    ego_vehicle_.y = previous_path_y_[prev_path_size - 1];

    //we need to calculate ref_yaw because provided ego_vehicle_.yaw is of
    //where the vehicle is right now (which is somewhere before previous_path end)
    //so ego_vehicle_.yaw does not represent angle that will be when ego vehicle arrives
    //at the end of previous path
    //Angle is tangent between last and second
    //last point of the previous path so get the second last point
    double prev_x = previous_path_x_[prev_path_size - 2];
    double prev_y = previous_path_y_[prev_path_size - 2];

    //calculate tangent
    ego_vehicle_.yaw = atan2(ego_vehicle_.y - prev_y, ego_vehicle_.x - prev_x);
  }
}

PathPlanner::~PathPlanner() {
  // TODO Auto-generated destructor stub
}

double PathPlanner::FindDistanceFromVehicleAhead() {
  //  int ego_vehicle_lane = MapUtils::GetLane(ego_vehicle_.d);
  double min_distance = 999999;
  double ego_vehicle_s = previous_path_x_.size() > 0 ? previous_path_last_s_: ego_vehicle_.s;

  for (int i = 0; i < vehicles_.size(); ++i) {
    int vehicle_lane = vehicles_[i].lane;
    double s = vehicles_[i].state_at(previous_path_x_.size() * 0.02)[1];
    //discard this iteration if vehicle is not in ego_vehicle's lane
    //and is not leading vehicle

    if (vehicle_lane != this->lane_
        || s <= ego_vehicle_s) {
      //we are only interested in leading vehicles in same lane
      continue;
    }

    //vehicle is leading vehicle and in same lane as ego vehicle
    //check distance to leading vehicle
    double distance = s - ego_vehicle_s;
    if (distance < min_distance) {
      min_distance = distance;
    }
  }

  return min_distance;
}

bool PathPlanner::IsTooCloseToVehicleAhead() {
  return FindDistanceFromVehicleAhead() < BUFFER_DISTANCE;
}

CartesianTrajectory PathPlanner::GenerateTrajectory(const Vehicle &ego_vehicle,
                                           const vector<vector<double> > &sensor_fusion_data,
                                           const vector<double> &previous_path_x,
                                           const vector<double> &previous_path_y,
                                           const double previous_path_last_s,
                                           const double previous_path_last_d) {
  this->ego_vehicle_ = ego_vehicle;
  this->previous_path_x_ = previous_path_x;
  this->previous_path_y_ = previous_path_y;
  this->previous_path_last_s_ = previous_path_last_s;
  this->previous_path_last_d_ = previous_path_last_d;

  this->vehicles_ = ExtractSensorFusionData(sensor_fusion_data, previous_path_x.size());

  //we need to consider whether Simulator has traversed previous path
  //completely or some points till left. This will affect ego vehicle
  //state as well as new path so let's update ego vehicle state accordingly
//  UpdateEgoVehicleStateWithRespectToPreviousPath();

  bool is_too_close_to_leading_vehicle = IsTooCloseToVehicleAhead();

  if (is_too_close_to_leading_vehicle) {
    //decrease speed to avoid collision
    this->reference_velocity_ -= 0.224;

    //also consider changing lanes
//    this->lane_ = (this->lane_ + 1) % 3;
  } else if (reference_velocity_ < SPEED_LIMIT) {
    //we have less than desired speed and
    //are more than buffer distance from leading vehicle
    //so that means we can increase speed
    this->reference_velocity_ += 0.224;
  }


////  stay in current lane
//   CartesianTrajectory trajectory = trajectory_generator_.GenerateTrajectory(ego_vehicle_, previous_path_x, previous_path_y, previous_path_last_s,
//          previous_path_last_d, lane_, reference_velocity_);
//
//      return trajectory;

  CartesianTrajectory trajectory = FindBestTrajectory();
  return trajectory.ExtractTrajectory();
}

vector<CartesianTrajectory> PathPlanner::GeneratePossibleTrajectories(const vector<int> &valid_lanes) {
  //find trajectories for valid lanes
  vector<CartesianTrajectory> trajectories;

  const int lanes_count = valid_lanes.size();
  for (int i = 0; i < lanes_count; ++i) {
    int proposed_lane = valid_lanes[i];

    //generate trajectory for this lane
    CartesianTrajectory trajectory = trajectory_generator_.GenerateTrajectory(
        ego_vehicle_, previous_path_x_, previous_path_y_, previous_path_last_s_,
        previous_path_last_d_, proposed_lane, reference_velocity_);
    trajectories.push_back(trajectory);
  }

  return trajectories;
}

vector<int> PathPlanner::GetPossibleLanesToGo() {
  //filter out valid lanes to go to
  vector<int> valid_lanes = { lane_ };
  if (lane_ == 0) {
    //we only want to change one lane at a time
    valid_lanes.push_back(1);
  } else if (lane_ == 2) {
    //we only want to change one lane at a time
    valid_lanes.push_back(1);
  } else if (lane_ == 1) {
    //we can either go left lane or right lane
    valid_lanes.push_back(0);
    valid_lanes.push_back(2);
  }

  return valid_lanes;
}

CartesianTrajectory PathPlanner::FindBestTrajectory() {

  //filter out valid lanes to go to
  vector<int> valid_lanes = GetPossibleLanesToGo();
  cout << "\n\n--current lane is " << lane_ << " and next valid lanes are: " << endl;
  Utils::print_vector(valid_lanes);
  //find possible lanes to go on
  vector<CartesianTrajectory> possible_trajectories  = GeneratePossibleTrajectories(valid_lanes);

  //now find min cost trajectory out of these possible trajectories
  int best_trajectory_index = -1;
  double min_cost = 999999;

  const int trajectories_count = possible_trajectories.size();
  for (int i = 0; i < trajectories_count; ++i) {
    cout << "----------Considering trajectory for lane: " << possible_trajectories[i].lane << "----------"<< endl;

    double cost = cost_functions_.CalculateCost(ego_vehicle_, vehicles_, possible_trajectories[i], this->lane_);
    printf("---cost of lane %d is %f\n", possible_trajectories[i].lane, cost);

    if (cost < min_cost) {
      min_cost = cost;
      best_trajectory_index = i;
    }
  }

  CartesianTrajectory best_trajectory = possible_trajectories[best_trajectory_index];

  printf("selected lane %d with cost %f\n", best_trajectory.lane, min_cost);
  if (this->lane_ != best_trajectory.lane) {
    cerr << "Lane change occurred" << endl;
  }
  this->lane_ = best_trajectory.lane;
  return best_trajectory;
}



