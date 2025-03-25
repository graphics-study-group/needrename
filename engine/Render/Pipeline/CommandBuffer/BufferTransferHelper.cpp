#include "BufferTransferHelper.h"

namespace Engine {
    vk::MemoryBarrier2 BufferTransferHelper::GetBufferBarrier(BufferTransferType type)
    {
        return vk::MemoryBarrier2{
            GetScope1(type).first, GetScope1(type).second,
            GetScope2(type).first, GetScope2(type).second
        };
    }
    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> BufferTransferHelper::GetScope1(BufferTransferType type)
    {
        switch(type) {
        case BufferTransferType::VertexBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eVertexInput, 
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead
            );
        case BufferTransferType::VertexAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite
            );
        case BufferTransferType::ReadonlyStorageBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderStorageRead
            );
        case BufferTransferType::ReadonlyStorageAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite
            );
        case BufferTransferType::ClearAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eClear,
                vk::AccessFlagBits2::eTransferWrite
            );
        default:
            assert(false && "Unexpected buffer transfer type.");
            return std::make_pair(
                vk::PipelineStageFlagBits2::eNone,
                vk::AccessFlagBits2::eNone
            );
        }
    }
    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> BufferTransferHelper::GetScope2(BufferTransferType type)
    {
        switch(type) {
        case BufferTransferType::VertexBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite
            );
        case BufferTransferType::VertexAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eVertexInput, 
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead
            );
        case BufferTransferType::ReadonlyStorageBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite
            );
        case BufferTransferType::ReadonlyStorageAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eVertexShader, 
                vk::AccessFlagBits2::eShaderStorageRead
            );
        case BufferTransferType::ClearAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite
            );
        default:
            assert(false && "Unexpected buffer transfer type.");
            return std::make_pair(
                vk::PipelineStageFlagBits2::eNone,
                vk::AccessFlagBits2::eNone
            );
        }
    }
}
