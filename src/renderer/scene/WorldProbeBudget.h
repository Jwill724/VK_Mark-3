#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>

inline std::array<uint32_t, 5> DistributeWorldProbeBudget(uint32_t budget, float falloff)
{
	std::array<uint32_t, 5> result{};
	budget = std::min(budget, 10240u);
	falloff = std::clamp(falloff, 0.01f, 1.0f);
	if (budget >= 5u)
	{
		result.fill(1u);
		budget -= 5u;
	}
	while (budget != 0u)
	{
		double total = 0.0;
		std::array<double, 5> weights{}, remainder{};
		for (uint32_t c = 0; c < 5; ++c)
		{
			weights[c] = result[c] < 2048u ? std::pow(double(falloff), double(c)) : 0.0;
			total += weights[c];
		}
		const uint32_t batch = budget;
		for (uint32_t c = 0; c < 5; ++c)
		{
			const double quota = double(batch) * weights[c] / total;
			const uint32_t n = std::min(uint32_t(std::floor(quota)), 2048u - result[c]);
			result[c] += n;
			budget -= n;
			remainder[c] = result[c] < 2048u ? quota - std::floor(quota) : -1.0;
		}
		for (uint32_t i = 0u; i < 5u && budget; ++i)
		{
			const auto it = std::max_element(remainder.begin(), remainder.end());
			if (*it < 0.0) break;
			++result[std::size_t(it - remainder.begin())];
			*it = -1.0;
			--budget;
		}
	}
	return result;
}
