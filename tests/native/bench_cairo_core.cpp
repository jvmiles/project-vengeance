// SPDX-License-Identifier: GPL-2.0-only
// Reproducible single-thread benchmark of CDraw vs the GDrawCairo core on identical work.
// Matrix: shape kind x antialiasing x target format x shape size. Each cell renders the
// same seeded shapes with each renderer; the renderers are interleaved within every
// repetition (so machine-load changes affect them alike) and the median of 9
// repetitions per renderer is reported in microseconds per shape.
// With XARA_CAIRO_BASELINE (set by run_cairo_bench.sh --baseline <commit>) an older core,
// compiled into namespace XaraCairoBase, is measured in the same runs: a paired
// before/after comparison.
//   tests/native/run_cairo_bench.sh [--baseline <commit>] [results.csv]
// Building with -DXARA_CAIRO_PROFILE additionally prints the core's per-phase times.
#include "gdraw_reference.h"
#include "cairocore.h"
#ifdef XARA_CAIRO_BASELINE
#include "baseline/cairocore.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static const INT32 W = 1024, H = 768, U = kRefUnitsPerPixel;
static const int kReps = 9;

struct Shape { std::vector<POINT> P; std::vector<BYTE> V; };
enum Kind { POLY_FILL, CURVE_FILL, POLY_STROKE, CURVE_STROKE, KINDS };
static const char* const kKindNames[] = { "polygon fill", "curve fill", "polyline stroke", "curve stroke" };

static std::vector<Shape> Workload(int K, bool Large, int Count)
{
	std::mt19937 rng(1000 + K * 2 + Large);
	std::uniform_real_distribution<double> X(0, W), Y(0, H), R(Large ? 40 : 3, Large ? 220 : 18), J(-1, 1);
	std::vector<Shape> v;
	for (int n = 0; n < Count; n++)
	{
		Shape s;
		const double cx = X(rng), cy = Y(rng), rx = R(rng), ry = R(rng);
		if (K == CURVE_FILL || K == CURVE_STROKE)
		{
			const double k = 0.5522847498;
			const double p[13][2] = { {rx,0},{rx,k*ry},{k*rx,ry},{0,ry},{-k*rx,ry},{-rx,k*ry},{-rx,0},{-rx,-k*ry},{-k*rx,-ry},{0,-ry},{k*rx,-ry},{rx,-k*ry},{rx,0} };
			for (int i = 0; i < 13; i++) { s.P.push_back({ INT32((cx + p[i][0]) * U), INT32((cy + p[i][1]) * U) }); s.V.push_back(i ? PT_BEZIERTO : PT_MOVETO); }
		}
		else
			for (int i = 0; i < 7; i++)
			{
				const double a = i * 2 * M_PI / 7;
				s.P.push_back({ INT32((cx + rx * std::cos(a) * (1 + 0.3 * J(rng))) * U), INT32((cy + ry * std::sin(a) * (1 + 0.3 * J(rng))) * U) });
				s.V.push_back(i ? PT_LINETO : PT_MOVETO);
			}
		if (K == POLY_FILL || K == CURVE_FILL || K == CURVE_STROKE) s.V.back() |= PT_CLOSEFIGURE;
		v.push_back(s);
	}
	return v;
}

static double Median(std::vector<double> v)
{
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

template <class F> static double TimeMs(F f)
{
	const auto a = std::chrono::steady_clock::now();
	f();
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

// Run Shapes through one core (current or baseline namespace) on its own bitmap.
template <class R, class P, class C, class J>
static void RunCore(R& r, const std::vector<Shape>& Shapes, bool Stroke, C Cap, J Join)
{
	for (const Shape& s : Shapes)
		Stroke ? r.StrokePath(reinterpret_cast<const P*>(s.P.data()), s.V.data(), UINT32(s.P.size()), false, 2 * U, Cap, Join)
			   : r.FillPath(reinterpret_cast<const P*>(s.P.data()), s.V.data(), UINT32(s.P.size()), 0);
}

int main(int argc, char** argv)
{
	FILE* pCsv = argc > 1 ? std::fopen(argv[1], "w") : NULL;
	if (pCsv) std::fprintf(pCsv, "kind,aa,target,size,shapes,cdraw_us_per_shape,cairo_us_per_shape,ratio,baseline_us_per_shape,baseline_ratio\n");
	std::printf("%dx%d target, median of %d repetitions, microseconds per shape\n", W, H, kReps);
	std::printf("%-16s %-3s %-6s %-5s %7s %9s %9s %7s %9s %7s %7s\n", "kind", "aa", "target", "size", "shapes", "CDraw", "Cairo", "ratio",
				"baseline", "b-ratio", "speedup");
	double LogSum = 0, LogBase = 0, LogSpeed = 0;
	int Cells = 0;
	for (int K = 0; K < KINDS; K++)
		for (int Large = 0; Large <= 1; Large++)
			for (int AA = 1; AA >= 0; AA--)
				for (int RGBT = 0; RGBT <= 1; RGBT++)
				{
					const int Count = Large ? 400 : 3000;
					const std::vector<Shape> Shapes = Workload(K, Large, Count);
					const bool Stroke = (K == POLY_STROKE || K == CURVE_STROKE);
					const DWORD Fill = RGBT ? 0xff000000 : 0x00ffffff;
					const DWORD Compression = RGBT ? 0x80000001 : BI_RGB;
					const GMATRIX m = RefPixelMatrix();
					const UINT32 Flatness = AA ? U / 10 : U / 2;
					RefGDraw g(W, H, false, Fill, Compression);
					GDraw_SetMatrix(g.Context(), &m);
					GDraw_SetAntialiasFlag(g.Context(), AA);
					GDraw_SetFlatness(g.Context(), Flatness);
					GDraw_SetHintingFlag(g.Context(), true);
					GColour_SetColour(g.Context(), RGB(30, 90, 200));
					std::vector<DWORD> px(size_t(W) * H, Fill);
					XaraCairo::Renderer r;
					r.SetTarget(px.data(), W, H, false, RGBT != 0);
					r.SetMatrix({ m.AX, m.AY, m.BX, m.BY, m.CX, m.CY });
					r.SetAntialias(AA);
					r.SetFlatness(Flatness);
					r.SetHinting(true);
					r.SetColour(RGB(30, 90, 200));
#ifdef XARA_CAIRO_BASELINE
					std::vector<DWORD> pxb(size_t(W) * H, Fill);
					XaraCairoBase::Renderer rb;
					rb.SetTarget(pxb.data(), W, H, false, RGBT != 0);
					rb.SetMatrix({ m.AX, m.AY, m.BX, m.BY, m.CX, m.CY });
					rb.SetAntialias(AA);
					rb.SetFlatness(Flatness);
					rb.SetHinting(true);
					rb.SetColour(RGB(30, 90, 200));
#endif
					std::vector<double> tR, tC, tB;
					for (int Rep = 0; Rep < kReps; Rep++)
					{
						tR.push_back(TimeMs([&] {
							for (const Shape& s : Shapes)
								Stroke ? GDraw_StrokePath(g.Context(), s.P.data(), s.V.data(), DWORD(s.P.size()), false, 2 * U, CAPS_ROUND, JOIN_ROUND, NULL)
									   : GDraw_FillPath(g.Context(), s.P.data(), s.V.data(), DWORD(s.P.size()), 0);
						}));
						tC.push_back(TimeMs([&] { RunCore<XaraCairo::Renderer, XaraCairo::DevicePoint>(r, Shapes, Stroke, XaraCairo::CAP_ROUND, XaraCairo::JOIN_ROUND); }));
#ifdef XARA_CAIRO_BASELINE
						tB.push_back(TimeMs([&] { RunCore<XaraCairoBase::Renderer, XaraCairoBase::DevicePoint>(rb, Shapes, Stroke, XaraCairoBase::CAP_ROUND, XaraCairoBase::JOIN_ROUND); }));
#endif
					}
					const double tRef = Median(tR), tCairo = Median(tC);
					const double tBase = tB.empty() ? 0 : Median(tB);
					const double usRef = tRef * 1000 / Count, usCairo = tCairo * 1000 / Count, usBase = tBase * 1000 / Count;
					std::printf("%-16s %-3s %-6s %-5s %7d %9.2f %9.2f %6.2fx", kKindNames[K], AA ? "on" : "off", RGBT ? "rgbt" : "bi_rgb",
								Large ? "large" : "small", Count, usRef, usCairo, usCairo / usRef);
					if (tBase > 0)
						std::printf(" %9.2f %6.2fx %6.2fx", usBase, usBase / usRef, usBase / usCairo);
					std::printf("\n");
					if (pCsv)
						std::fprintf(pCsv, "%s,%d,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f\n", kKindNames[K], AA, RGBT ? "rgbt" : "bi_rgb", Large ? "large" : "small",
									 Count, usRef, usCairo, usCairo / usRef, usBase, tBase > 0 ? usBase / usRef : 0);
					LogSum += std::log(usCairo / usRef);
					if (tBase > 0) { LogBase += std::log(usBase / usRef); LogSpeed += std::log(usBase / usCairo); }
					Cells++;
				}
	std::printf("geometric mean Cairo/CDraw ratio over %d cells: %.2fx\n", Cells, std::exp(LogSum / Cells));
#ifdef XARA_CAIRO_BASELINE
	std::printf("geometric mean baseline/CDraw ratio: %.2fx; current core speedup over baseline: %.2fx\n",
				std::exp(LogBase / Cells), std::exp(LogSpeed / Cells));
#endif
#ifdef XARA_CAIRO_PROFILE
	XaraCairo::PrintProfile();
#endif
	if (pCsv) std::fclose(pCsv);
	return 0;
}
