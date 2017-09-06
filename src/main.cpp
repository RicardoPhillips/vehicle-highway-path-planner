#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen/Core"
#include "Eigen/QR"
#include "Eigen/Dense"
#include "json.hpp"
#include "spline.h"
#include "vehicle.h"
#include "utils.h"
#include "polynomial_trajectory_generator.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, vector<double> maps_x, vector<double> maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, vector<double> maps_s, vector<double> maps_x, vector<double> maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

void TransformToVehicleCoordinates(double ref_x, double ref_y, double ref_yaw, double &x, double &y);

void TransformFromVehicleToMapCoordinates(double ref_x, double ref_y, double ref_yaw, double &x, double &y);

int UdacityCode();
int MyCode();

int main() {
  MyCode();
  return 0;
}

void TransformToVehicleCoordinates(double ref_x, double ref_y, double ref_yaw, double &x, double &y) {
  //translate (x, y) to where vehicle is right now (ref_x, ref_y)
  double shift_x = x - ref_x;
  double shift_y = y - ref_y;

  //rotate (x, y) by ref_yaw in clockwise so that vehicle is at angle 0
  double new_x = shift_x * cos(0-ref_yaw) - shift_y * sin(0-ref_yaw);
  double new_y = shift_x * sin(0-ref_yaw) + shift_y * cos(0-ref_yaw);

  //update passed params (x, y)
  x = new_x;
  y = new_y;
}

void TransformFromVehicleToMapCoordinates(double ref_x, double ref_y, double ref_yaw, double &x, double &y) {
  //rotate (x, y) by ref_yaw in counter-clockwise so that vehicle is at angle ref_yaw
  double rotated_x = x * cos(ref_yaw) - y * sin(ref_yaw);
  double rotated_y = x * sin(ref_yaw) + y * cos(ref_yaw);

  //translate (x, y) to where vehicle is right now (ref_x, ref_y) in map coordinates
  double new_x = rotated_x + ref_x;
  double new_y = rotated_y + ref_y;

  //update passed params (x, y)
  x = new_x;
  y = new_y;
}

int UdacityCode() {
  uWS::Hub h;

    // Load up map values for waypoint's x,y,s and d normalized normal vectors
    vector<double> map_waypoints_x;
    vector<double> map_waypoints_y;
    vector<double> map_waypoints_s;
    vector<double> map_waypoints_dx;
    vector<double> map_waypoints_dy;

    // Waypoint map to read from
    string map_file_ = "data/highway_map.csv";
    // The max s value before wrapping around the track back to 0
    double max_s = 6945.554;

    ifstream in_map_(map_file_.c_str(), ifstream::in);

    if (!in_map_.is_open()) {
      cerr << "Unable to open map file" << endl;
      exit(-1);
    }

    string line;
    while (getline(in_map_, line)) {
      istringstream iss(line);
      double x;
      double y;
      float s;
      float d_x;
      float d_y;
      iss >> x;
      iss >> y;
      iss >> s;
      iss >> d_x;
      iss >> d_y;
      map_waypoints_x.push_back(x);
      map_waypoints_y.push_back(y);
      map_waypoints_s.push_back(s);
      map_waypoints_dx.push_back(d_x);
      map_waypoints_dy.push_back(d_y);
    }

    cout << "map reading complete" << endl;

    //define some initial states
    //start lane: 0 means far left lane, 1 means middle lane, 2 means right lane
    int lane = 1;

    //define SPEED LIMIT
    const int SPEED_LIMIT = 50; //mph

    //define desired velocity
    double ref_velocity = 0; //mph

    h.onMessage([&ref_velocity, &lane, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy]
                 (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length, uWS::OpCode opCode) {
      // "42" at the start of the message means there's a websocket message event.
      // The 4 signifies a websocket message
      // The 2 signifies a websocket event
      //auto sdata = string(data).substr(0, length);
      //cout << sdata << endl;
      if (length && length > 2 && data[0] == '4' && data[1] == '2') {

        auto s = hasData(data);

        if (s != "") {
          auto j = json::parse(s);

          string event = j[0].get<string>();

          if (event == "telemetry") {
            // j[1] is the data JSON object

            // Main car's localization Data
              double car_x = j[1]["x"];
              double car_y = j[1]["y"];
              double car_s = j[1]["s"];
              double car_d = j[1]["d"];
              double car_yaw = j[1]["yaw"];
              double car_speed = j[1]["speed"];

              // Previous path data given to the Planner
              auto previous_path_x = j[1]["previous_path_x"];
              auto previous_path_y = j[1]["previous_path_y"];
              // Previous path's end s and d values
              double end_path_s = j[1]["end_path_s"];
              double end_path_d = j[1]["end_path_d"];

              // Sensor Fusion Data, a list of all other cars on the same side of the road.
              //The data format for each car is: [ id, x, y, vx, vy, s, d]
              vector<vector<double>> sensor_fusion = j[1]["sensor_fusion"];

              bool is_too_close = false;
              for (int i = 0; i < sensor_fusion.size(); ++i) {
                //d-coordinate is at index 6
                double other_vehicle_d = sensor_fusion[i][6];
                //s-coordinate is at index 5
                double other_vehicle_s = sensor_fusion[i][5];
                //access vx, vy which are at indexes (3, 4)
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];

                //check if this car is in my lane
                //I am adding +2 or -2 to (2+4*lane) formula because other
                //vehicle may not be in lane center and this formula is for lane center
                //so adding +2 or -2 makes the boundary exactly 4 meters so even if
                //vehicle is not at lane center if it is in 4 meter range of lane it
                //will be considered as in-lane vehicle
                if (other_vehicle_d < (2+4*lane+2) && other_vehicle_d > (2+4*lane-2)) {
                  //as vehicle is in our lane, so let's predict its `s`
                  double other_vehicle_speed = sqrt(vx*vx + vy*vy);
                  //predict s = s + delta_t * v
                  //also multiply (delta_t * v) term with prev_path_size
                  //as simulator may not have completed previous path
                  //so vehicle may not be there yet where we are expecting it to be
                  double other_vehicle_predicted_s = other_vehicle_s + 0.02 * other_vehicle_speed * previous_path_x.size();

                  //check if vehicle is infront of ego vehicle
                  //and distance between that vehicle and our vehicle is less than 30 meters
                  if (other_vehicle_predicted_s > car_s && (other_vehicle_s - car_s) < 30) {
                    is_too_close = true;
                  }
                }

              }


              if (is_too_close) {
                //if collision danger then decrease speed
                ref_velocity -= 0.224;
              } else if (ref_velocity < 49.5) {
                //if no collision danger and we are under speed limit
                //then increase speed
                ref_velocity += 0.244;
              }

              /***********Process Data****************/

              //reference point where the car is right now
              double ref_x = car_x;
              double ref_y = car_y;
              double ref_yaw = deg2rad(car_yaw);

              vector<double> points_x;
              vector<double> points_y;

              //by the time we receive this data simulator may not have
              //completed our previous 50 points so we are going to consider
              //them here as well to make transition smooth
              int prev_path_size = previous_path_x.size();

              //check if there are any points in previous path
              if (prev_path_size < 2) {
                //there are not enough points so consider where the car is right now
                //and where the car was before that (current point) point
                //so predict past for delta_t = 1
                double prev_x = car_x - cos(car_yaw);
                double prev_y = car_y - sin(car_yaw);

                //add point where the car was 1 timestep before
                points_x.push_back(prev_x);
                points_y.push_back(prev_y);

                //add point where is car right now
                points_x.push_back(car_x);
                points_y.push_back(car_y);
              } else {
                 //as previous path is still not completed by simulator so
                //consider end of previous path as reference point for car
                //(instead of current point which is somewhere in previous path)
                //to calculate
                ref_x = previous_path_x[prev_path_size - 1];
                ref_y = previous_path_y[prev_path_size - 1];

                //get the point before reference point (2nd last point in prev path)
                double x_before_ref_x = previous_path_x[prev_path_size - 2];
                double y_before_ref_y = previous_path_y[prev_path_size - 2];

                //as these two points make a tangent line to the car
                //so we can calculate car's yaw angle using these
                //two points
                ref_yaw = atan2(ref_y - y_before_ref_y, ref_x - x_before_ref_x);

                //add point where the car was before reference point to list of points
                points_x.push_back(x_before_ref_x);
                points_y.push_back(y_before_ref_y);

                //add the last point in previous path
                points_x.push_back(ref_x);
                points_y.push_back(ref_y);
              }

              //add 3 more points, each spaced 30m from other in Frenet coordinates
              //start from where the car is right now
              //Remember: each lane is 4m wide and we want the car to be in middle of lane
              double d_coord_for_middle_lane = (2 + 4*lane);
              vector<double> next_wp0 = getXY(car_s + 30, d_coord_for_middle_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
              vector<double> next_wp1 = getXY(car_s + 60, d_coord_for_middle_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
              vector<double> next_wp2 = getXY(car_s + 90, d_coord_for_middle_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);

              //add these 3 points to way points list
              points_x.push_back(next_wp0[0]);
              points_y.push_back(next_wp0[1]);

              points_x.push_back(next_wp1[0]);
              points_y.push_back(next_wp1[1]);

              points_x.push_back(next_wp2[0]);
              points_y.push_back(next_wp2[1]);


              //to make our math easier, let's convert these points
              //from Global maps coordinates to vehicle coordinates
              for (int i = 0; i < points_x.size(); ++i) {
                TransformToVehicleCoordinates(ref_x, ref_y, ref_yaw, points_x[i], points_y[i]);
              }

              //spline to fit a curve through the way points we have
              //spline is fitting that makes sure that the curve passes
              //through the all the points
              tk::spline spline;

              //fit a spline through the ways points
              //we will use this fitting to get any point on
              //this line (extrapolation of points)
              spline.set_points(points_x, points_y);

              //now that we have a spline fitting we
              //can get any point on this line
              //(if given x, this spline will give corresponding y)
              //but we still have to space our points on spline
              //so that we can achieve our desired velocity

              //our reference velocity is in miles/hour we need to
              //convert our desired/reference velocity in meters/second for ease
              //Remember 1 mile = 1.69 km = 1609.34 meters
              //and 1 hours = 60 mins * 60 secs = 3600
              double ref_velocity_in_meters_per_second = ref_velocity * (1609.34 / (60*60));
              cout << "ref velocity m/s: " << ref_velocity_in_meters_per_second << endl;

              //as we need to find the space between points on spline to
              //to keep our desired velocity, to achieve that
              //we can define some target on x-axis, target_x,
              //and then find how many points should be there in between
              //x-axis=0 and x-axis=target_x so that if we keep our desired
              //velocity and each time step is 0.02 (20 ms) long then we achieve
              //our target distance between 0 to target_x, target_dist
              //
              //formula: V = d / t
              //as each timestep = 0.02 secs so
              //formula: V = d / (0.02 * N)
              //--> ref_v = target_dist / (0.02 * N)
              //--> N = target_dist / (0.02 * ref_v)

              double target_x = 30.0;
              //get the target_x's corresponding y-point on spline
              double target_y = spline(target_x);
              double target_dist = sqrt(target_x*target_x + target_y*target_y);
              double N = target_dist/ (0.02 * ref_velocity_in_meters_per_second);

              //here N is: number of points from 0 to target_dist_x
              //
              //--> point_space = target_x / N
              double point_space = target_x / N;


              //now we are ready to generate trajectory points
              vector<double> next_x_vals;
              vector<double> next_y_vals;

              //but first let's add points of previous_path
              //as they have not yet been traversed by simulator
              //and we considered its end point as the reference point
              for (int i = 0; i < prev_path_size; ++i) {
                next_x_vals.push_back(previous_path_x[i]);
                next_y_vals.push_back(previous_path_y[i]);
              }

              //now we can generate the remaining points (50 - prev_path.size)
              //using the spline and point_space

              //as we are in vehicle coordinates so first x is 0
              double x_start = 0;

              for (int i = 0; i < 50 - prev_path_size; ++i) {
                double point_x = x_start + point_space;
                double point_y = spline(point_x);

                //now the current x is the new start x
                x_start = point_x;

                //convert this point to Global map coordinates which
                //is what simulator expects
                TransformFromVehicleToMapCoordinates(ref_x, ref_y, ref_yaw, point_x, point_y);

                //add this point to way points list
                next_x_vals.push_back(point_x);
                next_y_vals.push_back(point_y);
              }

              /***************END Processing of data***************/

              json msgJson;

              // define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
              msgJson["next_x"] = next_x_vals;
              msgJson["next_y"] = next_y_vals;

              auto msg = "42[\"control\","+ msgJson.dump()+"]";

              //this_thread::sleep_for(chrono::milliseconds(1000));
              ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);

          }
        } else {
          // Manual driving
          std::string msg = "42[\"manual\",{}]";
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      }
    });

    // We don't need this since we're not using HTTP but if it's removed the
    // program
    // doesn't compile :-(
    h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                       size_t, size_t) {
      const std::string s = "<h1>Hello world!</h1>";
      if (req.getUrl().valueLength == 1) {
        res->end(s.data(), s.length());
      } else {
        // i guess this should be done more gracefully?
        res->end(nullptr, 0);
      }
    });

    h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
      std::cout << "Connected!!!" << std::endl;
    });

    h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                           char *message, size_t length) {
      ws.close();
      std::cout << "Disconnected" << std::endl;
    });

    int port = 4567;
    if (h.listen(port)) {
      std::cout << "Listening to port " << port << std::endl;
    } else {
      std::cerr << "Failed to listen to port" << std::endl;
      return -1;
    }
    h.run();

    return 0;
}

int MyCode() {
  uWS::Hub h;

    // Load up map values for waypoint's x,y,s and d normalized normal vectors
    vector<double> map_waypoints_x;
    vector<double> map_waypoints_y;
    vector<double> map_waypoints_s;
    vector<double> map_waypoints_dx;
    vector<double> map_waypoints_dy;

    // Waypoint map to read from
    string map_file_ = "data/highway_map.csv";
    // The max s value before wrapping around the track back to 0
    double max_s = 6945.554;

    ifstream in_map_(map_file_.c_str(), ifstream::in);

    if (!in_map_.is_open()) {
      cerr << "Unable to open map file" << endl;
      exit(-1);
    }

    string line;
    while (getline(in_map_, line)) {
      istringstream iss(line);
      double x;
      double y;
      float s;
      float d_x;
      float d_y;
      iss >> x;
      iss >> y;
      iss >> s;
      iss >> d_x;
      iss >> d_y;
      map_waypoints_x.push_back(x);
      map_waypoints_y.push_back(y);
      map_waypoints_s.push_back(s);
      map_waypoints_dx.push_back(d_x);
      map_waypoints_dy.push_back(d_y);
    }

    cout << "map reading complete" << endl;

    //define some initial states
    //start lane: 0 means far left lane, 1 means middle lane, 2 means right lane
    int lane = 1;

    //define SPEED LIMIT
    const int SPEED_LIMIT = 50; //mph

    //define desired velocity
    double ref_velocity = 0; //mph

    h.onMessage([&ref_velocity, &lane, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy]
                 (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length, uWS::OpCode opCode) {
      // "42" at the start of the message means there's a websocket message event.
      // The 4 signifies a websocket message
      // The 2 signifies a websocket event
      //auto sdata = string(data).substr(0, length);
      //cout << sdata << endl;
      if (length && length > 2 && data[0] == '4' && data[1] == '2') {

        auto s = hasData(data);

        if (s != "") {
          auto j = json::parse(s);

          string event = j[0].get<string>();

          if (event == "telemetry") {
            // j[1] is the data JSON object

            // Main car's localization Data
              double car_x = j[1]["x"];
              double car_y = j[1]["y"];
              double car_s = j[1]["s"];
              double car_d = j[1]["d"];
              double car_yaw = j[1]["yaw"];
              double car_speed = j[1]["speed"];

              // Previous path data given to the Planner
              vector<double> previous_path_x = j[1]["previous_path_x"];
              vector<double> previous_path_y = j[1]["previous_path_y"];
              // Previous path's end s and d values
              double end_path_s = j[1]["end_path_s"];
              double end_path_d = j[1]["end_path_d"];

              // Sensor Fusion Data, a list of all other cars on the same side of the road.
              //The data format for each car is: [ id, x, y, vx, vy, s, d]
              vector<vector<double>> sensor_fusion = j[1]["sensor_fusion"];

              bool is_too_close = false;
              vector<Vehicle> vehicles;

              for (int i = 0; i < sensor_fusion.size(); ++i) {
                Vehicle vehicle(sensor_fusion[i]);
                vehicles.push_back(vehicle);

                //d-coordinate is at index 6
                double other_vehicle_d = sensor_fusion[i][6];
                //s-coordinate is at index 5
                double other_vehicle_s = sensor_fusion[i][5];
                //access vx, vy which are at indexes (3, 4)
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];

//                printf("i, vx, vy: %d, %f, %f", i, vx, vy);

                //check if this car is in my lane
                //I am adding +2 or -2 to (2+4*lane) formula because other
                //vehicle may not be in lane center and this formula is for lane center
                //so adding +2 or -2 makes the boundary exactly 4 meters so even if
                //vehicle is not at lane center if it is in 4 meter range of lane it
                //will be considered as in-lane vehicle
                if (other_vehicle_d < (2+4*lane+2) && other_vehicle_d > (2+4*lane-2)) {
                  //as vehicle is in our lane, so let's predict its `s`
                  double other_vehicle_speed = sqrt(vx*vx + vy*vy);
                  //predict s = s + delta_t * v
                  //also multiply (delta_t * v) term with prev_path_size
                  //as simulator may not have completed previous path
                  //so vehicle may not be there yet where we are expecting it to be
                  double other_vehicle_predicted_s = other_vehicle_s + 0.02 * other_vehicle_speed * previous_path_x.size();

                  //check if vehicle is infront of ego vehicle
                  //and distance between that vehicle and our vehicle is less than 30 meters
                  if (other_vehicle_predicted_s > car_s && (other_vehicle_s - car_s) < 30) {
                    is_too_close = true;
                  }
                }
              }

              VectorXd start_s(3);
              start_s << car_s, 0, 0;

              VectorXd start_d(3);
              start_d << car_d, 0, 0;

              VectorXd delta(6);
              delta << 20, 0, 0, 0, 0, 0;

              int index = Utils::find_nearest_vehicle_ahead(vehicles, car_s, car_d);
              printf("Nearest vehicle id: %d\n", index);
              cout << endl;

              if (index == -1) {
                cout << "Creating a dummy vehicle" << endl;
                VectorXd start_state(6);
                start_state << car_s+30, 0, 0, (2+lane*4), 0, 0;

                Vehicle dummy_v(start_state);
                vehicles.push_back(dummy_v);

                index = vehicles.size() - 1;
              }

              PolynomialTrajectoryGenerator ptg;
              Trajectory best = ptg.generate_trajectory(start_s, start_d, index, delta, 5, vehicles);
              cout << "found best trajectory" << endl;
              vector<vector<double> > pts = Utils::get_trajectory_points(best, 50);

              vector<double> next_x_vals;
              vector<double> next_y_vals;

              cout << "total points: " << pts[0].size() << endl;
              for (int i = 0; i < pts[0].size(); ++i) {
                vector<double> point = getXY(pts[0][i], pts[1][i], map_waypoints_s, map_waypoints_x, map_waypoints_y);
                next_x_vals.push_back(point[0]);
                next_y_vals.push_back(point[1]);
              }

//              if (is_too_close) {
//                //if collision danger then decrease speed
//                ref_velocity -= 0.224;
//              } else if (ref_velocity < 49.5) {
//                //if no collision danger and we are under speed limit
//                //then increase speed
//                ref_velocity += 0.244;
//              }
//
//              /***********Process Data****************/
//
//              //reference point where the car is right now
//              double ref_x = car_x;
//              double ref_y = car_y;
//              double ref_yaw = deg2rad(car_yaw);
//
//              vector<double> points_x;
//              vector<double> points_y;
//
//              //by the time we receive this data simulator may not have
//              //completed our previous 50 points so we are going to consider
//              //them here as well to make transition smooth
//              int prev_path_size = previous_path_x.size();
//
//              //check if there are any points in previous path
//              if (prev_path_size < 2) {
//                //there are not enough points so consider where the car is right now
//                //and where the car was before that (current point) point
//                //so predict past for delta_t = 1
//                double prev_x = car_x - cos(car_yaw);
//                double prev_y = car_y - sin(car_yaw);
//
//                //add point where the car was 1 timestep before
//                points_x.push_back(prev_x);
//                points_y.push_back(prev_y);
//
//                //add point where is car right now
//                points_x.push_back(car_x);
//                points_y.push_back(car_y);
//              } else {
//                 //as previous path is still not completed by simulator so
//                //consider end of previous path as reference point for car
//                //(instead of current point which is somewhere in previous path)
//                //to calculate
//                ref_x = previous_path_x[prev_path_size - 1];
//                ref_y = previous_path_y[prev_path_size - 1];
//
//                //get the point before reference point (2nd last point in prev path)
//                double x_before_ref_x = previous_path_x[prev_path_size - 2];
//                double y_before_ref_y = previous_path_y[prev_path_size - 2];
//
//                //as these two points make a tangent line to the car
//                //so we can calculate car's yaw angle using these
//                //two points
//                ref_yaw = atan2(ref_y - y_before_ref_y, ref_x - x_before_ref_x);
//
//                //add point where the car was before reference point to list of points
//                points_x.push_back(x_before_ref_x);
//                points_y.push_back(y_before_ref_y);
//
//                //add the last point in previous path
//                points_x.push_back(ref_x);
//                points_y.push_back(ref_y);
//              }
//
//              //add 3 more points, each spaced 30m from other in Frenet coordinates
//              //start from where the car is right now
//              //Remember: each lane is 4m wide and we want the car to be in middle of lane
//              double d_coord_for_middle_lane = (2 + 4*lane);
//              vector<double> next_wp0 = getXY(car_s + 30, d_coord_for_middle_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
//              vector<double> next_wp1 = getXY(car_s + 60, d_coord_for_middle_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
//              vector<double> next_wp2 = getXY(car_s + 90, d_coord_for_middle_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
//
//              //add these 3 points to way points list
//              points_x.push_back(next_wp0[0]);
//              points_y.push_back(next_wp0[1]);
//
//              points_x.push_back(next_wp1[0]);
//              points_y.push_back(next_wp1[1]);
//
//              points_x.push_back(next_wp2[0]);
//              points_y.push_back(next_wp2[1]);
//
//
//              //to make our math easier, let's convert these points
//              //from Global maps coordinates to vehicle coordinates
//              for (int i = 0; i < points_x.size(); ++i) {
//                TransformToVehicleCoordinates(ref_x, ref_y, ref_yaw, points_x[i], points_y[i]);
//              }
//
//              //spline to fit a curve through the way points we have
//              //spline is fitting that makes sure that the curve passes
//              //through the all the points
//              tk::spline spline;
//
//              //fit a spline through the ways points
//              //we will use this fitting to get any point on
//              //this line (extrapolation of points)
//              spline.set_points(points_x, points_y);
//
//              //now that we have a spline fitting we
//              //can get any point on this line
//              //(if given x, this spline will give corresponding y)
//              //but we still have to space our points on spline
//              //so that we can achieve our desired velocity
//
//              //our reference velocity is in miles/hour we need to
//              //convert our desired/reference velocity in meters/second for ease
//              //Remember 1 mile = 1.69 km = 1609.34 meters
//              //and 1 hours = 60 mins * 60 secs = 3600
//              double ref_velocity_in_meters_per_second = ref_velocity * (1609.34 / (60*60));
////              cout << "ref velocity m/s: " << ref_velocity_in_meters_per_second << endl;
//
//              //as we need to find the space between points on spline to
//              //to keep our desired velocity, to achieve that
//              //we can define some target on x-axis, target_x,
//              //and then find how many points should be there in between
//              //x-axis=0 and x-axis=target_x so that if we keep our desired
//              //velocity and each time step is 0.02 (20 ms) long then we achieve
//              //our target distance between 0 to target_x, target_dist
//              //
//              //formula: V = d / t
//              //as each timestep = 0.02 secs so
//              //formula: V = d / (0.02 * N)
//              //--> ref_v = target_dist / (0.02 * N)
//              //--> N = target_dist / (0.02 * ref_v)
//
//              double target_x = 30.0;
//              //get the target_x's corresponding y-point on spline
//              double target_y = spline(target_x);
//              double target_dist = sqrt(target_x*target_x + target_y*target_y);
//              double N = target_dist/ (0.02 * ref_velocity_in_meters_per_second);
//
//              //here N is: number of points from 0 to target_dist_x
//              //
//              //--> point_space = target_x / N
//              double point_space = target_x / N;
//
//
//              //now we are ready to generate trajectory points
//              vector<double> next_x_vals;
//              vector<double> next_y_vals;
//
//              //but first let's add points of previous_path
//              //as they have not yet been traversed by simulator
//              //and we considered its end point as the reference point
//              for (int i = 0; i < prev_path_size; ++i) {
//                next_x_vals.push_back(previous_path_x[i]);
//                next_y_vals.push_back(previous_path_y[i]);
//              }
//
//              //now we can generate the remaining points (50 - prev_path.size)
//              //using the spline and point_space
//
//              //as we are in vehicle coordinates so first x is 0
//              double x_start = 0;
//
//              for (int i = 0; i < 50 - prev_path_size; ++i) {
//                double point_x = x_start + point_space;
//                double point_y = spline(point_x);
//
//                //now the current x is the new start x
//                x_start = point_x;
//
//                //convert this point to Global map coordinates which
//                //is what simulator expects
//                TransformFromVehicleToMapCoordinates(ref_x, ref_y, ref_yaw, point_x, point_y);
//
//                //add this point to way points list
//                next_x_vals.push_back(point_x);
//                next_y_vals.push_back(point_y);
//              }

              /***************END Processing of data***************/

              json msgJson;

              // define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
              msgJson["next_x"] = next_x_vals;
              msgJson["next_y"] = next_y_vals;

              auto msg = "42[\"control\","+ msgJson.dump()+"]";

              //this_thread::sleep_for(chrono::milliseconds(1000));
              ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);

          }
        } else {
          // Manual driving
          std::string msg = "42[\"manual\",{}]";
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      }
    });

    // We don't need this since we're not using HTTP but if it's removed the
    // program
    // doesn't compile :-(
    h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                       size_t, size_t) {
      const std::string s = "<h1>Hello world!</h1>";
      if (req.getUrl().valueLength == 1) {
        res->end(s.data(), s.length());
      } else {
        // i guess this should be done more gracefully?
        res->end(nullptr, 0);
      }
    });

    h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
      std::cout << "Connected!!!" << std::endl;
    });

    h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                           char *message, size_t length) {
      ws.close();
      std::cout << "Disconnected" << std::endl;
    });

    int port = 4567;
    if (h.listen(port)) {
      std::cout << "Listening to port " << port << std::endl;
    } else {
      std::cerr << "Failed to listen to port" << std::endl;
      return -1;
    }
    h.run();

    return 0;
}

















































































