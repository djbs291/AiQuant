#pragma once
#ifndef FIN_CORE_VOLUME_HPP
#define FIN_CORE_VOLUME_HPP

namespace fin::core
{
    class Volume
    {
    public:
        explicit Volume(double v);
        double value() const;

        Volume operator+(const Volume &other) const;

    private:
        double val_;
    };
}

#endif // FIN_CORE_VOLUME_HPP
