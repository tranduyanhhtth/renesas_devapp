/*******************************************************************************
 * traffic_violation/src/tracking/VehicleTracker.cpp
 * IoU greedy tracker – nhận tất cả det, tự filter vehicle class
 ******************************************************************************/
#include "VehicleTracker.h"
#include <algorithm>

VehicleTracker::VehicleTracker(const DetectorConfig& det_cfg,
                               int max_missed, float iou_thresh)
    : m_det(det_cfg), m_max_missed(max_missed), m_iou_thresh(iou_thresh)
{}

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
    std::vector<bool> matched_trk(m_tracks.size(), false);

    /* Greedy IoU matching (highest IoU first) */
    std::vector<std::tuple<float,int,int>> scores;
    for (int t=0;t<(int)m_tracks.size();++t)
        for (int d=0;d<(int)vdets.size();++d) {
            float iou = computeIou(m_tracks[t].bbox, vdets[d].bbox);
            if (iou >= m_iou_thresh) scores.emplace_back(iou,t,d);
        }
    std::sort(scores.begin(),scores.end(),[](auto&a,auto&b){return std::get<0>(a)>std::get<0>(b);});

    for (auto& [iou,t,d] : scores) {
        if (matched_trk[t] || matched_det[d]) continue;
        matched_trk[t] = matched_det[d] = true;
        m_tracks[t].bbox = vdets[d].bbox;
        m_tracks[t].missed_frames = 0;
        cv::Point2f c = {vdets[d].bbox.x, vdets[d].bbox.y};
        m_tracks[t].trajectory.push_back(c);
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
        m_tracks.push_back(nv);
    }

    /* Prune stale tracks */
    for (int t=0;t<(int)m_tracks.size();++t)
        if (!matched_trk[t]) ++m_tracks[t].missed_frames;
    m_tracks.erase(
        std::remove_if(m_tracks.begin(),m_tracks.end(),
            [&](const TrackedVehicle& v){ return v.missed_frames > m_max_missed; }),
        m_tracks.end());

    return m_tracks;
}

void VehicleTracker::setPlate(int id, const std::string& plate) {
    for (auto& v : m_tracks)
        if (v.track_id == id && !plate.empty()) { v.plate = plate; return; }
}
void VehicleTracker::setHelmet(int id, bool h) {
    for (auto& v : m_tracks) if (v.track_id == id) { v.has_helmet = h; return; }
}
