#pragma once

class NoCopy{
public:
    NoCopy(){}

    // 禁止拷贝构造，赋值等
    NoCopy(const NoCopy&) = delete;
    NoCopy& operator=(const NoCopy&) = delete;

    NoCopy(NoCopy&&) = delete;
    NoCopy& operator=(NoCopy&&) = delete;
};