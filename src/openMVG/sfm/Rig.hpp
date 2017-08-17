#pragma once

#include <openMVG/types.hpp>
#include <openMVG/geometry/pose3.hpp>

#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

#include <vector>
#include <cassert>

namespace openMVG {
namespace sfm {

enum class ERigSubPoseStatus: std::uint8_t
{
  UNINITIALIZED = 0
  , ESTIMATED = 1
  , CONSTANT = 2
};

struct RigSubPose
{
  /// status of the sub-pose
  ERigSubPoseStatus status = ERigSubPoseStatus::UNINITIALIZED;
  /// relative pose of the sub-pose
  geometry::Pose3 pose;

  /**
   * @brief RigSubPose constructor
   * @param pose The relative pose of the sub-pose
   * @param status The status of the sub-pose
   */
  RigSubPose(const geometry::Pose3& pose = geometry::Pose3(),
             ERigSubPoseStatus status = ERigSubPoseStatus::UNINITIALIZED)
    : pose(pose)
    , status(status)
  {}

  /**
   * @brief operator ==
   * @param other the other RigSubPose
   * @return true if the current RigSubPose and the other are identical.
   */
  bool operator==(const RigSubPose& other) const
  {
    return (status == other.status &&
            pose == other.pose);
  }

  /**
   * @brief cereal serialize method
   * @param ar The archive
   */
  template <class Archive>
  void serialize(Archive & ar)
  {
    ar(cereal::make_nvp("status", status),
       cereal::make_nvp("pose", pose));
  }
};

class Rig
{
public:

  /**
   * @brief Rig constructor
   * @param nbSubPoses The number of sub-poses of the rig
   */
  explicit Rig(unsigned int nbSubPoses = 0)
  {
    _subPoses.resize(nbSubPoses);
  }


  /**
   * @brief operator ==
   * @param other the other Rig
   * @return true if the current rig and the other are identical.
   */
  bool operator==(const Rig& other) const
  {
    return _subPoses == other._subPoses;
  }

  /**
   * @brief Check if the rig has at least one sub-pose initialized
   * @return true if at least one subpose initialized
   */
  bool isInitialized() const
  {
    for(const RigSubPose& subPose : _subPoses)
    {
      if(subPose.status != ERigSubPoseStatus::UNINITIALIZED)
        return true;
    }
    return false;
  }

  /**
   * @brief Get the number of sub-poses in the rig
   * @return number of sub-poses in the rig
   */
  std::size_t getNbSubPoses() const
  {
    return _subPoses.size();
  }

  /**
   * @brief Get the sub-poses const vector
   * @return rig sub-poses
   */
  const std::vector<RigSubPose>& getSubPoses() const
  {
    return _subPoses;
  }

  /**
   * @brief Get the sub-pose for the given sub-pose index
   * @param index The sub-pose index
   * @return corresponding rig sub-pose
   */
  const RigSubPose& getSubPose(IndexT index) const
  {
    return _subPoses.at(index);
  }

  /**
   * @brief Get the sub-pose for the given sub-pose index
   * @param index The sub-pose index
   * @return corresponding rig sub-pose
   */
  RigSubPose& getSubPose(IndexT index)
  {
    return _subPoses.at(index);
  }

  /**
   * @brief Set the given sub-pose for the given sub-pose index
   * @param index The sub-pose index
   * @param rigSubPose The rig sub-pose
   */
  void setSubPose(IndexT index, const RigSubPose& rigSubPose)
  {
    assert(_subPoses.size() > index);
    _subPoses[index] = rigSubPose;
  }

  /**
   * @brief cereal serialize method
   * @param ar The archive
   */
  template <class Archive>
  void serialize(Archive & ar)
  {
    ar(cereal::make_nvp("subposes", _subPoses));
  }

private:

  /// rig sub-poses
  std::vector<RigSubPose> _subPoses;
};

} // namespace sfm
} // namespace openMVG
