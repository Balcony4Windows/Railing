#pragma once
#include <vector>
#include <complex>
#include <cmath>

class SimpleFFT {
	const double PI = 3.141592653589793238460;
public:
	using Complex = std::complex<double>;
	using CArray = std::vector<Complex>;

	void BitReverseCopy(const CArray &source, CArray &dest)
	{
		int n = (int)source.size();
		for (int i = 0; i < n; i++) {
			int rev = 0;
			int k = 0;
			for (int j = 1; j < n; j <<= 1) {
				rev <<= 1;
				if (k & 1) rev |= 1;
				k >>= 1;
			}
			dest[i] = source[rev];
		}
	}

	/// <summary>
	/// Cooley-Turkey FFT Implementation
	/// </summary>
	/// <param name="buffer">To transform</param>
	void Transform(CArray &buffer)
	{
		size_t n = buffer.size();
		if (n <= 1) return;

		int N = (int)n;
		int k = N, n2 = 1;
		int logN = 0;
		while ((k >>= 1) > 0) logN++;

		int j = 0; // Swap elements
		for (int i = 0; i < N - 1; i++) {
			if (i < j) std::swap(buffer[i], buffer[j]);
			int m = N / 2;
			while (j >= m && m > 1) {
				j -= m;
				m /= 2;
			}
			j += m;
		}

		for (int s = 1; s <= logN; s++) {
			int m = 1 << s;
			int m2 = m >> 1;
			Complex w(1, 0);
			Complex wm = std::exp(Complex(0, -PI / m2));
			for (int j = 0; j < m2; j++) {
				for (int k = j; k < N; k += m) {
					Complex t = w * buffer[k + m2];
					Complex u = buffer[k];
					buffer[k] = u + t;
					buffer[k + m2] = u - t;
				}
				w *= wm;
			}
		}
	}
	/// <summary>
	/// Converts Raw Audio (PCM Float) to Frequency Magnitudes
	/// </summary>
	/// <param name="samples">Raw audio chunk (must be power of 2)</param>
	/// <param name="output">Will fill with values 0.0-1.0</param>
	void Compute(const std::vector<float> &samples, std::vector<float> &output)
	{
		size_t n = samples.size();
		CArray data(n);

		for (size_t i = 0; i < n; i++) {
			double window = 0.5 * (1.0 - std::cos(2.0 * PI * i / (n - 1)));
			data[i] = Complex(samples[i] * window, 0);
		}

		Transform(data);

		output.resize(n / 2);
		for (size_t i = 0; i < n / 2; i++) {
			double mag = std::abs(data[i]); // sqrt(re^2 + im^2)
			mag /= n;

			float val = (float)(20.0 * std::log10(mag + 1e-6)); // Convert to decibals

			val = (val + 60.0f) / 60.0f; // -60db is silence.
			if (val < 0) val = 0;
			if (val > 1) val = 1;

			output[i] = val;
		}
	}
};