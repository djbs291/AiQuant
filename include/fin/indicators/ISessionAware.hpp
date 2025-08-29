// include/fin/indicators/ISessionAware.hpp
#pragma once
namespace fin::indicators
{
    class ISessionAware
    {
    public:
        virtual ~ISessionAware() = default;
        virtual void reset_session() = 0;
    };
} // namespace fin::indicators
