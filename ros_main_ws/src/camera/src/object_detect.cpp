#include "object_detect.hpp"
#include "index.hpp"

float cv_detect::min_max_unsigned(float value, float min, float max)
{
    return (value - min) / (max - min);
}

float cv_detect::min_max_signed(float value, float min, float max)
{
    return 2.f * min_max_unsigned(value, min, max) - 1;
}

cv::Mat cv_detect::region_of_interest(const cv::Mat & image)
{
    // extract the ROI (polygon/trapezoid)
    // create a mask to use when copying the result
    // see: http://www.pieter-jan.com/node/5
    cv::Mat mask = cv::Mat::zeros(image.rows, image.cols, CV_8UC1);
    // adjust ROI
    cv::Point ptss[4] = {
        cv::Point(180, 280),
        cv::Point(0,   470),
        cv::Point(640, 470),
        cv::Point(460, 280)
    };
    cv::fillConvexPoly(mask, ptss, 4, cv::Scalar(255, 8, 0));
    cv::Mat result(image.rows, image.cols, CV_8UC1);
    image.copyTo(result, mask);
    cvtColor(result, result, CV_RGB2GRAY);
    return result;
}

std::string cv_detect::contour_pixels(const cv::Mat & gray)
{
    cv::RNG rng(12345);
    cv::blur(gray, gray, cv::Size(3,3));
    cv::Mat markers(gray.size(), CV_32S);
    markers = cv::Scalar::all(0);
    cv::Mat threshold_output;

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::threshold(gray, threshold_output, 50, 255, cv::THRESH_BINARY);
    cv::findContours(threshold_output, 
                     contours, 
                     hierarchy,
                     cv:: RETR_CCOMP, 
                     cv::CHAIN_APPROX_TC89_KCOS, 
                     cv::Point(0, 0));
    cv::Mat drawing = cv::Mat::zeros(threshold_output.size(), CV_8UC3);
    for (int i = 0; i < contours.size(); i++) {
        cv::Scalar color = cv::Scalar(rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255));
        cv::drawContours(drawing, contours, i, color, 3, 8, std::vector<cv::Vec4i>(), 0, cv::Point());
    }
    float pixels = (float)cv::countNonZero(region_of_interest(drawing));
    // trapezoid area is A: (a + b) 2 * h
    // where a = top base (180px), b = bottom base (640px) and h = height (190px)
    // min-max to from 0 - 77900(Area) to 0 ~ 1
    nlohmann::json j = {{"contours", min_max_unsigned(pixels, 0.f, 77900)}};
    return j.dump();
}

std::string cv_detect::find_lines(const cv::Mat & image)
{
    
    cv::Mat copy(image);
    cv::Mat img;
    cv::Mat found;
    cv::Canny(image, img, 50, 200, 3);
    cv::cvtColor(img, found, CV_GRAY2BGR);
    std::vector<cv::Vec4i> lines;
    // 1 px = 1point, 
    // theta = PI/180
    // min # of intersections = 50
    // min # of points forming a line = 100
    // max # of gap points inside a line = 5
    cv::HoughLinesP(img, lines, 1, CV_PI/180, 50, 80, 5);
    nlohmann::json j_array, j_result;
    //std::vector<std::tuple<int, int, float, float>> data;
    std::vector<cv_detect::line> data;

    for (std::size_t i = 0; i < lines.size(); i++) {
        cv::Vec4i l = lines[i];
        // if either Y is too high up => ignore
        if (l[1] <= 100 || l[3] <= 100) {
            continue;
        }
        // draw lines on `found` if needed
        cv::line(copy, cv::Point(l[0],l[1]), cv::Point(l[2],l[3]), cv::Scalar(255, 255, 0), 3, 0);

        // calculate for each line, its magnitude and angle
        float dx = l[2] - l[0];
        float dy = l[3] - l[1];
        float theta = atan(dy/dx) * 180.f / M_PI;

        // calculate size of line
        float length = std::sqrt(std::pow(dx, 2) + std::pow(dy, 2));
        if (theta != 0 && length != 0) {
            cv_detect::line line_data;
            // training data
            line_data.x = min_max_unsigned(l[0], 0, image.cols);
            line_data.y = min_max_unsigned(l[1], 0, image.rows);
            line_data.yaw = min_max_signed(theta, -180, 180);
            line_data.size = min_max_unsigned(length, 0, image.cols);
            data.push_back(line_data);
        }
    }
    ///sort largest lines
    cv_detect::line l_data;
    std::sort(data.begin(), data.end(), l_data);

    //add to json
    int counter = 0;
    for (const auto each_line : data) {
        j_array.push_back({{"x", each_line.x},
                           {"y", each_line.y},
                           {"yaw", each_line.yaw},
                           {"size", each_line.size}});
        counter++;
        if (counter > 4) {
            break;
        }
    }

    j_result = {{"lines", j_array}};

    return j_result.dump();
}

std::string cv_detect::find_red_circle(const cv::Mat & image,
                                       const cv::Mat & gray)
{
    cv::Mat blurred, colors;
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);
    std::vector<cv::Vec3f> circles;
    nlohmann::json j;

    cv::HoughCircles(blurred, circles, CV_HOUGH_GRADIENT, 1, 5, 50, 50, 0, 0);
    //std::cout << "circles: " << circles.size() << std::endl;
    for (std::size_t i = 0; i < circles.size(); i++) {
        // TODO: verify the color inside the circle (look at image, not blurred or grey)
        //cv::circle(blurred, cv::Point(circles[i][0], circles[i][1]), circles[i][2], cv::Scalar(0,255,0), 1, 0, 0);
        //create a mask with circle
        cv::Mat mask = cv::Mat::zeros( image.rows, image.cols, CV_8UC1 );
        cv::circle(mask, cv::Point(circles[i][0], circles[i][1]), circles[i][2], cv::Scalar(255,255,255), -1, 8, 0 ); //-1 means filled
        cv::Scalar mean = cv::mean(image, mask);        
        if (mean(2) >= 120 && mean(1) < 100 && mean(0) < 100) {
             j = {{"traffic", 1}}; //red light on
        } 
        else {
             j = {{"traffic", 0}}; //red light off
              //std::cout << mean(2) << " " << mean(1) << " " << mean(0) << std::endl; 
        }
    }
    if (circles.size() == 0) {
        j = {{"traffic", 0}};
    }

    return j.dump();
}

cv_detect::orb::orb(const cv::Mat & model)
: orb__(cv::ORB::create(500, 1.2f ,8 ,15 ,0, 2, cv::ORB::HARRIS_SCORE, 31)),
  model__(model)
{ 
    cv::Mat gray = cv::Mat(model__.size(), CV_8UC1);
    cv::cvtColor(model__, gray, cv::COLOR_RGB2GRAY);
    orb__->detect(gray, model_keys__, model_desc__);
}

cv::Mat cv_detect::orb::match(
                                const cv::Mat & image,
                                const cv::Mat & gray
                             )
{
    std::vector<cv::KeyPoint> frame_keys;
    cv::Mat frame_desc;
    orb__->detect(gray, frame_keys, frame_desc);
    cv::BFMatcher matcher(cv::NORM_HAMMING);
    std::vector<cv::DMatch> matches;
    matcher.match(model_desc__, frame_desc, matches);
    std::vector<cv::Point2f> model_points, frame_points;

    for (int i = 0; i < matches.size(); i++) {
        model_points.push_back(model_keys__[matches[i].queryIdx].pt);
        frame_points.push_back(frame_keys[matches[i].trainIdx].pt);
    }
    cv::Rect box = cv::boundingRect(frame_points);
    // TODO: draw box on frame
    /*
    cv::Matx33f H = cv::findHomography(model_points, frame_points, CV_RANSAC);
    std::vector<cv::Point> model_border, frame_border;
    model_border.push_back(cv::Point(0, 0));
    model_border.push_back(cv::Point(0, model__.rows));
    model_border.push_back(cv::Point(model__.cols, model__.rows));
    model_border.push_back(cv::Point(model__.cols, 0));
    for (size_t i = 0; i < model_border.size(); i++) {
        cv::Vec3f p = H * cv::Vec3f(model_border[i].x, model_border[i].y, 1);
        frame_border.push_back(cv::Point(p[0] / p[2], p[1] / p[2]));
    }
    cv::polylines(image, frame_border, true, CV_RGB(0, 255, 0));
    */
    cv::Mat img_matches;
    cv::drawMatches(model__, model_keys__, 
                    image, frame_keys,
                    matches, 
                    img_matches);

    return img_matches;
}

cv_detect::qr_scan::qr_scan()
{
    scanner__.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
}

std::vector<cv_detect::qr> cv_detect::qr_scan::scan(const cv::Mat & gray)
{
    int width = gray.cols;
    int height = gray.rows;
    unsigned char *raw = (unsigned char*)(gray.data);
    zbar::Image frame(width, height, "Y800", raw, width * height);
    scanner__.scan(frame);

    std::vector<qr> qrs;
    for (zbar::Image::SymbolIterator symbol = frame.symbol_begin(); 
         symbol != frame.symbol_end(); 
         ++symbol)
    {
        qr item;
        item.data = symbol->get_data();
        item.top_right.x = symbol->get_location_x(0);
        item.top_right.y = symbol->get_location_y(0);
        item.top_left.x = symbol->get_location_x(1);
        item.top_left.y= symbol->get_location_y(1);
        item.bot_left.x  = symbol->get_location_x(2);
        item.bot_left.y  = symbol->get_location_y(2);
        item.bot_right.x = symbol->get_location_x(3);
        item.bot_right.y = symbol->get_location_y(3);
        qrs.push_back(item);
    }
    return qrs;
}
