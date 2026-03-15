/*******************************************************************************
 * traffic_violation/src/tracking/VehicleTracker.cpp
 * IoU greedy tracker – nhận tất cả det, tự filter vehicle class
 ******************************************************************************/
#include "VehicleTracker.h"
#include <algorithm>
#include <cmath>

VehicleTracker::VehicleTracker(const DetectorConfig& det_cfg,
                               int max_missed, float iou_thresh)
    : m_det(det_cfg), m_max_missed(max_missed), m_iou_thresh(iou_thresh)
{}

void VehicleTracker::initKalman(int track_id, float cx, float cy)
{
    cv::KalmanFilter kf(4, 2, 0, CV_32F);
    kf.transitionMatrix = (cv::Mat_<float>(4,4) <<
        1,0,1,0,
        0,1,0,1,
        0,0,1,0,
        0,0,0,1);
    kf.measurementMatrix = (cv::Mat_<float>(2,4) <<
        1,0,0,0,
        0,1,0,0);
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));
    kf.statePost = (cv::Mat_<float>(4,1) << cx, cy, 0.f, 0.f);
    m_kalman[track_id] = kf;
    m_kalman_inited[track_id] = true;
}

cv::Point2f VehicleTracker::predictCenter(int track_id, const cv::Point2f& fallback)
{
    auto it = m_kalman.find(track_id);
    if (it == m_kalman.end()) return fallback;
    cv::Mat pred = it->second.predict();
    return {pred.at<float>(0), pred.at<float>(1)};
}

cv::Point2f VehicleTracker::correctCenter(int track_id, float cx, float cy)
{
    auto it = m_kalman.find(track_id);
    if (it == m_kalman.end()) {
        initKalman(track_id, cx, cy);
        return {cx, cy};
    }
    cv::Mat meas(2, 1, CV_32F);
    meas.at<float>(0) = cx;
    meas.at<float>(1) = cy;
    cv::Mat corr = it->second.correct(meas);
    return {corr.at<float>(0), corr.at<float>(1)};
}

float VehicleTracker::computeIou(const Box& a, const Box& b) {
    float ax1=a.x-a.w/2, ay1=a.y-a.h/2, ax2=a.x+a.w/2, ay2=a.y+a.h/2;
    float bx1=b.x-b.w/2, by1=b.y-b.h/2, bx2=b.x+b.w/2, by2=b.y+b.h/2;
    float inter = std::max(0.f,std::min(ax2,bx2)-std::max(ax1,bx1))
                * std::max(0.f,std::min(ay2,by2)-std::max(ay1,by1));
    float uni   = a.w*a.h + b.w*b.h - inter + 1e-6f;
    return inter / uni;
}

VehicleType VehicleTracker::classToType(int c) const {
    if (c == m_det.class_motorbike) return VehicleType::MOTORBIKE;
    if (c == m_det.class_car)       return VehicleType::CAR;
    if (c == m_det.class_truck)     return VehicleType::TRUCK;
    if (c == m_det.class_bus)       return VehicleType::BUS;
    if (c == m_det.class_person)    return VehicleType::MOTORBIKE; // rider on bike
    return VehicleType::UNKNOWN;
}

const std::vector<TrackedVehicle>& VehicleTracker::update(
    const std::vector<detection>& all_dets,
    int /*frame_w*/, int /*frame_h*/)
{
    /* Filter: chỉ track vehicle + person */
    std::vector<detection> vdets;
    for (auto& d : all_dets)
        if (m_det.isVehicle(d.c) || m_det.isRider(d.c))
            vdets.push_back(d);

    std::vector<bool> matched_det(vdets.size(), false);
    const int old_trk_count = (int)m_tracks.size();  /* save BEFORE adding new tracks */
    std::vector<bool> matched_trk(old_trk_count, false);

    std::vector<cv::Point2f> predicted_centers(old_trk_count);
    for (int t = 0; t < old_trk_count; ++t) {
        const auto& tr = m_tracks[t];
        predicted_centers[t] = predictCenter(tr.track_id, {tr.bbox.x, tr.bbox.y});
    }

    /* Greedy matching with Kalman-predicted center + IoU */
    std::vector<std::tuple<float,int,int>> scores;
    for (int t=0;t<old_trk_count;++t)
        for (int d=0;d<(int)vdets.size();++d) {
            Box pred_box = m_tracks[t].bbox;
            pred_box.x = predicted_centers[t].x;
            pred_box.y = predicted_centers[t].y;
            float iou = computeIou(pred_box, vdets[d].bbox);

            float dx = vdets[d].bbox.x - predicted_centers[t].x;
            float dy = vdets[d].bbox.y - predicted_centers[t].y;
            float dist = std::sqrt(dx*dx + dy*dy);

            if (iou >= m_iou_thresh || dist <= m_dist_thresh_px) {
                float dist_score = std::max(0.f, 1.f - dist / m_dist_thresh_px);
                float score = iou + 0.3f * dist_score;
                scores.emplace_back(score,t,d);
            }
        }
    std::sort(scores.begin(),scores.end(),[](auto&a,auto&b){return std::get<0>(a)>std::get<0>(b);});

    for (auto& [score,t,d] : scores) {
        if (matched_trk[t] || matched_det[d]) continue;
        matched_trk[t] = matched_det[d] = true;
        m_tracks[t].bbox = vdets[d].bbox;
        cv::Point2f corrected = correctCenter(m_tracks[t].track_id, vdets[d].bbox.x, vdets[d].bbox.y);
        m_tracks[t].bbox.x = corrected.x;
        m_tracks[t].bbox.y = corrected.y;
        m_tracks[t].missed_frames = 0;
        m_tracks[t].trajectory.push_back(corrected);
        if (m_tracks[t].trajectory.size() > 60)
            m_tracks[t].trajectory.erase(m_tracks[t].trajectory.begin());
    }

    /* New tracks for unmatched detections */
    for (int d=0;d<(int)vdets.size();++d) {
        if (matched_det[d]) continue;
        TrackedVehicle nv;
        nv.track_id    = m_next_id++;
        nv.type        = classToType(vdets[d].c);
        nv.bbox        = vdets[d].bbox;
        nv.has_helmet  = true;
        nv.missed_frames = 0;
        nv.trajectory.push_back({vdets[d].bbox.x, vdets[d].bbox.y});
        initKalman(nv.track_id, vdets[d].bbox.x, vdets[d].bbox.y);
        m_tracks.push_back(nv);
    }

    /* Prune stale tracks — only increment missed_frames for ORIGINAL tracks
     * (matched_trk covers [0, old_trk_count); new tracks are already missed=0) */
    for (int t=0;t<old_trk_count;++t) {
        if (!matched_trk[t]) {
            ++m_tracks[t].missed_frames;
            cv::Point2f pred = predictCenter(m_tracks[t].track_id, {m_tracks[t].bbox.x, m_tracks[t].bbox.y});
            m_tracks[t].bbox.x = pred.x;
            m_tracks[t].bbox.y = pred.y;
            m_tracks[t].trajectory.push_back(pred);
            if (m_tracks[t].trajectory.size() > 60)
                m_tracks[t].trajectory.erase(m_tracks[t].trajectory.begin());
        }
    }

    std::vector<int> removed_ids;
    for (const auto& v : m_tracks)
        if (v.missed_frames > m_max_missed) removed_ids.push_back(v.track_id);

    m_tracks.erase(
        std::remove_if(m_tracks.begin(),m_tracks.end(),
            [&](const TrackedVehicle& v){ return v.missed_frames > m_max_missed; }),
        m_tracks.end());

    for (int id : removed_ids) {
        m_kalman.erase(id);
        m_kalman_inited.erase(id);
    }

    return m_tracks;
}

void VehicleTracker::setPlate(int id, const std::string& plate) {
    for (auto& v : m_tracks)
        if (v.track_id == id && !plate.empty()) { v.plate = plate; return; }
}
void VehicleTracker::setHelmet(int id, bool h) {
    for (auto& v : m_tracks) if (v.track_id == id) { v.has_helmet = h; return; }
}
