/*
 * Created By: William Gu
 * Created On: Jan 20, 2018
 * Description: A path finding node which converts a given path into a Twist
 * message to send to the robot
 */

#include <PathToTwistNode.h>

PathToTwistNode::PathToTwistNode(int argc, char** argv, std::string node_name) {
    // Setup NodeHandles
    ros::init(argc, argv, node_name);
    ros::NodeHandle nh;
    ros::NodeHandle private_nh("~");
    uint32_t queue_size = 1;

    /*No received coordinates from tf on launch so not valid */
    valid_cood = false;

    std::string path_subscribe_topic = "/path"; // Setup subscriber to path
    path_subscriber                  = nh.subscribe(
    path_subscribe_topic, queue_size, &PathToTwistNode::pathCallBack, this);

    std::string tf_subscribe_topic = "/tf"; // Setup subscriber to tf
    tf_subscriber                  = nh.subscribe(
    tf_subscribe_topic, queue_size, &PathToTwistNode::tfCallBack, this);

    std::string twist_publish_topic =
    private_nh.resolveName("/cmd_vel"); // setup Publisher to twist
    twist_publisher =
    nh.advertise<geometry_msgs::Twist>(twist_publish_topic, queue_size);

    // Get Params
    SB_getParam(
    private_nh, "base_frame", base_frame, (std::string) "base_link");
    SB_getParam(
    private_nh, "global_frame", global_frame, (std::string) "odom_combined");
    SB_getParam(private_nh, "num_poses", num_poses, 10);
    SB_getParam(private_nh, "linear_speed_scaling_factor", linear_speed_scaling_factor, 1.0);
    SB_getParam(private_nh, "angular_speed_scaling_factor", angular_speed_scaling_factor, 1.0);
    SB_getParam(private_nh, "max_linear_speed", max_linear_speed, 1.0);
    SB_getParam(private_nh, "max_angular_speed", max_angular_speed, 1.0);
    SB_getParam(private_nh, "path_dropoff_factor", path_dropoff_factor, 20);

}

void PathToTwistNode::pathCallBack(const nav_msgs::Path::ConstPtr& path_ptr) {
    nav_msgs::Path path_msg =
    *path_ptr; // Take required information from received message
    geometry_msgs::Twist twist_msg = pathToTwist(path_msg,
                                                 robot_x_pos,
                                                 robot_y_pos,
                                                 robot_orientation,
                                                 num_poses,
                                                 valid_cood);
    twist_publisher.publish(twist_msg);
}

void PathToTwistNode::tfCallBack(const tf2_msgs::TFMessageConstPtr tf_message) {
    // Check if the tf_message contains the transform we're looking for
    for (geometry_msgs::TransformStamped tf_stamped : tf_message->transforms) {
        if (tf_stamped.header.frame_id == global_frame &&
            tf_stamped.child_frame_id == base_frame) {
            double x_pos = tf_stamped.transform.translation.x;
            double y_pos = tf_stamped.transform.translation.y;

            double quatx = tf_stamped.transform.rotation.x;
            double quaty = tf_stamped.transform.rotation.y;
            double quatz = tf_stamped.transform.rotation.z;
            double quatw = tf_stamped.transform.rotation.w;
            tf::Quaternion quat(quatx, quaty, quatz, quatw);
            tf::Matrix3x3 rot_matrix(quat);
            double roll, pitch, yaw;
            rot_matrix.getRPY(roll, pitch, yaw);

            // Set member variables
            robot_x_pos = x_pos;
            robot_y_pos = y_pos;
            robot_orientation =
            yaw; // Orientation = rotation about z axis (yaw)
            valid_cood = true;
        }
    }
}

geometry_msgs::Twist PathToTwistNode::pathToTwist(nav_msgs::Path path_msg,
                                              double x_pos,
                                              double y_pos,
                                              double orientation,
                                              int num_poses,
                                              bool valid_cood) {
    geometry_msgs::Twist twist_msg; // Initialize velocity message

    if (!valid_cood || path_msg.poses.size() == 0) { // No TF received yet so don't move
        twist_msg.linear.x  = 0;
        twist_msg.angular.z = 0;
        return twist_msg;
    }

    std::vector<geometry_msgs::PoseStamped> inc_poses = path_msg.poses;

    std::vector<float> x_vectors; // holds x values for vectors
    std::vector<float> y_vectors; // holds y values for vectors
    calcVectors(inc_poses,
                x_vectors,
                y_vectors,
                num_poses,
                x_pos,
                y_pos); // x_vectors and y_vectors now updated

    float x_sum = weightedSum(x_vectors, num_poses - 1, path_dropoff_factor); //-1 because number of vectors is one less than number of poses
    float y_sum = weightedSum(y_vectors, num_poses - 1, path_dropoff_factor);

    float desired_angle = atan(y_sum / x_sum);

    if (x_sum < 0) {
        desired_angle += M_PI;
    }

    float turn_rate = fmod(desired_angle - orientation,
                           2 * M_PI); // Keep turn_rate between -2pi and 2pi

    // If pi < turn < 2pi or -2pi < turn < -pi, turn in other direction instead
    if (turn_rate > M_PI)
        turn_rate -= 2 * M_PI;
    else if (turn_rate < -M_PI)
        turn_rate += 2 * M_PI;

    // At this point, turn rate should be between -pi and pi
    /*
    float speed =
    1.0 - fabs(fmod(turn_rate, M_PI) /
               (M_PI)); // Could multiply this by some factor to scale speed*/
    float speed = exp(-pow(turn_rate, 2) / 0.4);

    // Scale speeds
    speed *= linear_speed_scaling_factor;
    turn_rate *= angular_speed_scaling_factor;

    // Cap speeds
    speed = std::max(-(float)max_linear_speed, std::min(speed, (float)max_linear_speed));
    turn_rate = std::max(-(float)max_angular_speed, std::min(turn_rate, (float)max_angular_speed));

    twist_msg.linear.x  = speed;
    twist_msg.angular.z = turn_rate;
    return twist_msg;
}

void PathToTwistNode::calcVectors(
const std::vector<geometry_msgs::PoseStamped>& poses,
std::vector<float>& x_vectors,
std::vector<float>& y_vectors,
int num_poses,
double x_pos,
double y_pos) {
    x_vectors.push_back(poses[0].pose.position.x - x_pos);
    y_vectors.push_back(poses[0].pose.position.y - y_pos);
    for (int i = 1; i < num_poses; i++) {
        x_vectors.push_back(poses[i].pose.position.x -
                            poses[i - 1].pose.position.x);
        y_vectors.push_back(poses[i].pose.position.y -
                            poses[i - 1].pose.position.y);
    }
}

float PathToTwistNode::weightedSum(const std::vector<float>& vectors,
                               int num_to_sum, int path_dropoff_factor) {
    float weighted_sum = 0;
    for (int i = 0; i < num_to_sum; i++) {
        weighted_sum += vectors[i] / (i + 1); // 1/x scaling

        weighted_sum += vectors[i] * exp(-pow(i+1, 2)) / path_dropoff_factor;
    }
    return weighted_sum;
}
