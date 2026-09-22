#include <Core/Svg.h>
#include <GraphicalObjects/GuiTypes.h>
#include <Logger/Logger.h>
#include <vendor/tinyxml2/tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace RDA {
	namespace Svg {

		namespace {

			constexpr float kPi = 3.14159265358979323846f;
			// Rows sampled per pixel row. Sixteen is where an icon's diagonals stop
			// stepping visibly at 16px, which is the smallest size anybody draws one at.
			constexpr int kSubRows = 16;

			struct Point { float x = 0.0f, y = 0.0f; };

			// A 2x3 affine transform, applied as the document is read so that nothing
			// downstream has to carry a stack.
			struct Matrix {
				float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

				Point apply(Point p) const {
					return { a * p.x + c * p.y + e, b * p.x + d * p.y + f };
				}
				// Composition, `this` then `m` -- the order a nested transform needs.
				Matrix then(const Matrix& m) const {
					return { a * m.a + b * m.c,         a * m.b + b * m.d,
					         c * m.a + d * m.c,         c * m.b + d * m.d,
					         e * m.a + f * m.c + m.e,   e * m.b + f * m.d + m.f };
				}
				// How much this scales a length. A stroke drawn through a non-uniform
				// scale is not one width any more; the mean is what a renderer uses and
				// what an icon is drawn with.
				float scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
			};

			// A segment is a line or a cubic. Quadratics and arcs are turned into cubics
			// as they are read, so everything past the parser is one of two things.
			struct Segment {
				Point to;
				Point c1, c2;
				bool  curve = false;
			};

			struct SubPath {
				Point start;
				std::vector<Segment> segments;
				bool closed = false;
			};

			enum class Cap : uint8_t { Butt, Round, Square };
			enum class Join : uint8_t { Miter, Round, Bevel };

			struct Shape {
				std::vector<SubPath> paths;
				bool     hasFill = true;
				uint32_t fill = 0xFFFFFFFFu;     // AABBGGRR, as everywhere else here
				bool     evenOdd = false;
				bool     hasStroke = false;
				uint32_t stroke = 0xFFFFFFFFu;
				float    strokeWidth = 1.0f;
				Cap      cap = Cap::Butt;
				Join     join = Join::Miter;
				// A dashed stroke. Zero is solid. Animating the offset is how an icon draws
				// itself: one dash as long as the whole line, slid into view.
				float    dash = 0.0f;
				float    gap = 0.0f;
				float    dashOffset = 0.0f;
			};

			// What a shape inherits from its parents while the tree is walked.
			struct Inherited {
				Matrix   transform;
				bool     hasFill = true;
				uint32_t fill = 0xFFFFFFFFu;
				bool     hasStroke = false;
				uint32_t stroke = 0xFFFFFFFFu;
				float    strokeWidth = 1.0f;
				float    opacity = 1.0f;
				Cap      cap = Cap::Butt;
				Join     join = Join::Miter;
				bool     evenOdd = false;
				float    dash = 0.0f;
				float    gap = 0.0f;
				float    dashOffset = 0.0f;
			};

			// ---- reading numbers ------------------------------------------------------
			//
			// SVG path data is not tokenised the way anything else is: "M0 0L10 10" and
			// "M0,0 L10,10" and "M0-1" all mean the same thing, and a minus sign is a
			// separator as well as a sign. So this reads numbers and skips whatever is
			// between them rather than splitting first.
			struct Reader {
				const char* at;
				const char* end;

				void skip() {
					while (at < end && (*at == ' ' || *at == ',' || *at == '\t' ||
					                    *at == '\n' || *at == '\r')) ++at;
				}
				bool more() {
					skip();
					return at < end;
				}
				bool number(float& out) {
					skip();
					if (at >= end) return false;
					char* stop = nullptr;
					const float value = std::strtof(at, &stop);
					if (stop == at) return false;
					at = stop;
					out = value;
					return true;
				}
				// Two numbers, which is what most of the path grammar takes.
				bool pair(Point& out) { return number(out.x) && number(out.y); }
				// A flag in an arc is a single character, not a number: "1" and "0" run
				// straight into whatever follows with no separator.
				bool flag(bool& out) {
					skip();
					if (at >= end || (*at != '0' && *at != '1')) return false;
					out = (*at == '1');
					++at;
					return true;
				}
			};

			float parseLength(const char* text, float fallback) {
				if (!text || !*text) return fallback;
				char* stop = nullptr;
				const float value = std::strtof(text, &stop);
				if (stop == text) return fallback;
				// px, pt and the rest: an icon is drawn to a box, so the unit does not
				// survive past here and only the number matters.
				return value;
			}

			// `none`, `currentColor`, `#rgb`, `#rrggbb`, `rgb(r,g,b)` and the handful of
			// names an icon set actually uses. Anything else is white, which is the one
			// answer a tint can still colour.
			bool parsePaint(const char* text, uint32_t& out) {
				if (!text) return false;
				while (*text == ' ') ++text;
				if (std::strncmp(text, "none", 4) == 0) return false;
				if (std::strncmp(text, "currentColor", 12) == 0) {
					out = rgba(255, 255, 255);
					return true;
				}
				if (std::strncmp(text, "rgb", 3) == 0) {
					Reader r{ text + 3, text + std::strlen(text) };
					float c[3] = { 255, 255, 255 };
					for (int i = 0; i < 3; ++i) r.number(c[i]);
					out = rgba(static_cast<int>(c[0]), static_cast<int>(c[1]),
					           static_cast<int>(c[2]));
					return true;
				}
				uint32_t parsed = 0;
				if (parseColor(text, parsed)) { out = parsed; return true; }
				out = rgba(255, 255, 255);
				return true;
			}

			// ---- transforms ------------------------------------------------------------
			Matrix parseTransform(const char* text) {
				Matrix out;
				if (!text) return out;
				const char* at = text;
				const char* end = text + std::strlen(text);
				while (at < end) {
					while (at < end && (*at == ' ' || *at == ',')) ++at;
					const char* name = at;
					while (at < end && *at != '(') ++at;
					if (at >= end) break;
					const size_t length = static_cast<size_t>(at - name);
					++at;
					const char* argsEnd = at;
					while (argsEnd < end && *argsEnd != ')') ++argsEnd;

					Reader r{ at, argsEnd };
					float v[6] = { 0, 0, 0, 0, 0, 0 };
					int count = 0;
					while (count < 6 && r.number(v[count])) ++count;

					Matrix step;
					if (length >= 9 && std::strncmp(name, "translate", 9) == 0) {
						step = { 1, 0, 0, 1, v[0], count > 1 ? v[1] : 0.0f };
					} else if (length >= 5 && std::strncmp(name, "scale", 5) == 0) {
						step = { v[0], 0, 0, count > 1 ? v[1] : v[0], 0, 0 };
					} else if (length >= 6 && std::strncmp(name, "rotate", 6) == 0) {
						const float rad = v[0] * kPi / 180.0f;
						const float cs = std::cos(rad), sn = std::sin(rad);
						Matrix rot{ cs, sn, -sn, cs, 0, 0 };
						if (count >= 3) {
							// Around a point: out to it, turn, back.
							step = Matrix{ 1, 0, 0, 1, -v[1], -v[2] }
							           .then(rot)
							           .then(Matrix{ 1, 0, 0, 1, v[1], v[2] });
						} else {
							step = rot;
						}
					} else if (length >= 6 && std::strncmp(name, "matrix", 6) == 0) {
						step = { v[0], v[1], v[2], v[3], v[4], v[5] };
					} else if (length >= 5 && std::strncmp(name, "skewX", 5) == 0) {
						step = { 1, 0, std::tan(v[0] * kPi / 180.0f), 1, 0, 0 };
					} else if (length >= 5 && std::strncmp(name, "skewY", 5) == 0) {
						step = { 1, std::tan(v[0] * kPi / 180.0f), 0, 1, 0, 0 };
					}
					out = step.then(out);
					at = (argsEnd < end) ? argsEnd + 1 : end;
				}
				return out;
			}

			// ---- arcs --------------------------------------------------------------------
			//
			// The one part of the path grammar that is not already a bezier. Converted on
			// the way in, so nothing downstream knows arcs exist. This is the endpoint-to-
			// centre conversion from the specification's appendix, then one cubic per
			// quarter turn or less.
			void arcToCubics(Point from, Point to, float rx, float ry, float rotation,
			                 bool largeArc, bool sweep, std::vector<Segment>& out) {
				if (rx == 0.0f || ry == 0.0f) {
					out.push_back(Segment{ to, {}, {}, false });
					return;
				}
				rx = std::fabs(rx);
				ry = std::fabs(ry);
				const float rad = rotation * kPi / 180.0f;
				const float cs = std::cos(rad), sn = std::sin(rad);

				const float dx2 = (from.x - to.x) * 0.5f;
				const float dy2 = (from.y - to.y) * 0.5f;
				const float x1 = cs * dx2 + sn * dy2;
				const float y1 = -sn * dx2 + cs * dy2;

				// An ellipse too small to reach across is grown until it just can, which
				// is what the specification says to do rather than giving up.
				float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
				if (lambda > 1.0f) {
					const float root = std::sqrt(lambda);
					rx *= root;
					ry *= root;
				}

				const float denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
				float factor = 0.0f;
				if (denominator > 0.0f) {
					const float numerator = rx * rx * ry * ry - denominator;
					factor = std::sqrt((std::max)(0.0f, numerator / denominator));
				}
				if (largeArc == sweep) factor = -factor;

				const float cx1 = factor * rx * y1 / ry;
				const float cy1 = -factor * ry * x1 / rx;
				const float cx = cs * cx1 - sn * cy1 + (from.x + to.x) * 0.5f;
				const float cy = sn * cx1 + cs * cy1 + (from.y + to.y) * 0.5f;

				const auto angle = [](float ux, float uy, float vx, float vy) {
					const float dot = ux * vx + uy * vy;
					const float len = std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy);
					float a = std::acos(std::clamp(len > 0.0f ? dot / len : 0.0f, -1.0f, 1.0f));
					if (ux * vy - uy * vx < 0.0f) a = -a;
					return a;
				};

				const float start = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
				float sweepAngle = angle((x1 - cx1) / rx, (y1 - cy1) / ry,
				                         (-x1 - cx1) / rx, (-y1 - cy1) / ry);
				if (!sweep && sweepAngle > 0.0f) sweepAngle -= 2.0f * kPi;
				else if (sweep && sweepAngle < 0.0f) sweepAngle += 2.0f * kPi;

				const int pieces = (std::max)(1, static_cast<int>(
					std::ceil(std::fabs(sweepAngle) / (kPi * 0.5f))));
				const float step = sweepAngle / static_cast<float>(pieces);
				// The magic number that makes a cubic follow a circular arc: 4/3 tan(t/4).
				const float k = 4.0f / 3.0f * std::tan(step * 0.25f);

				float theta = start;
				Point at = from;
				for (int i = 0; i < pieces; ++i) {
					const float next = theta + step;
					const auto onEllipse = [&](float t) {
						const float ex = rx * std::cos(t);
						const float ey = ry * std::sin(t);
						return Point{ cs * ex - sn * ey + cx, sn * ex + cs * ey + cy };
					};
					const auto derivative = [&](float t) {
						const float ex = -rx * std::sin(t);
						const float ey = ry * std::cos(t);
						return Point{ cs * ex - sn * ey, sn * ex + cs * ey };
					};
					const Point end = onEllipse(next);
					const Point d0 = derivative(theta);
					const Point d1 = derivative(next);
					Segment seg;
					seg.curve = true;
					seg.c1 = { at.x + k * d0.x, at.y + k * d0.y };
					seg.c2 = { end.x - k * d1.x, end.y - k * d1.y };
					seg.to = end;
					out.push_back(seg);
					at = end;
					theta = next;
				}
			}

			// ---- the path grammar ---------------------------------------------------------
			std::vector<SubPath> parsePath(const char* data) {
				std::vector<SubPath> out;
				if (!data) return out;
				Reader r{ data, data + std::strlen(data) };

				SubPath current;
				bool open = false;
				Point at{ 0, 0 };
				Point subStart{ 0, 0 };
				Point lastControl{ 0, 0 };
				char last = 0;

				const auto flush = [&](bool closed) {
					if (open && !current.segments.empty()) {
						current.closed = closed;
						out.push_back(current);
					}
					current = SubPath{};
					open = false;
				};

				while (r.more()) {
					char command = *r.at;
					if ((command >= 'A' && command <= 'Z') || (command >= 'a' && command <= 'z')) {
						++r.at;
					} else {
						// A repeated command: "L10 10 20 20" is two lines, and after a
						// moveto the repeat is a lineto rather than another move.
						if (last == 'M') command = 'L';
						else if (last == 'm') command = 'l';
						else command = last;
						if (!command) break;
					}
					last = command;
					const bool relative = (command >= 'a' && command <= 'z');
					const char upper = static_cast<char>(relative ? command - 32 : command);
					const auto rel = [&](Point p) {
						return relative ? Point{ at.x + p.x, at.y + p.y } : p;
					};

					if (upper == 'M') {
						Point p;
						if (!r.pair(p)) break;
						flush(false);
						at = rel(p);
						subStart = at;
						current.start = at;
						open = true;
					} else if (upper == 'Z') {
						flush(true);
						at = subStart;
					} else if (upper == 'L') {
						Point p;
						if (!r.pair(p)) break;
						at = rel(p);
						current.segments.push_back(Segment{ at, {}, {}, false });
					} else if (upper == 'H' || upper == 'V') {
						float v;
						if (!r.number(v)) break;
						if (upper == 'H') at.x = relative ? at.x + v : v;
						else              at.y = relative ? at.y + v : v;
						current.segments.push_back(Segment{ at, {}, {}, false });
					} else if (upper == 'C' || upper == 'S') {
						Point c1, c2, p;
						if (upper == 'C') {
							if (!r.pair(c1)) break;
							c1 = rel(c1);
						} else {
							// Smooth: the first control is last one mirrored through the
							// current point, which is the whole reason S exists.
							c1 = { 2 * at.x - lastControl.x, 2 * at.y - lastControl.y };
						}
						if (!r.pair(c2) || !r.pair(p)) break;
						c2 = rel(c2);
						p = rel(p);
						Segment seg{ p, c1, c2, true };
						current.segments.push_back(seg);
						lastControl = c2;
						at = p;
						continue;
					} else if (upper == 'Q' || upper == 'T') {
						Point q, p;
						if (upper == 'Q') {
							if (!r.pair(q)) break;
							q = rel(q);
						} else {
							q = { 2 * at.x - lastControl.x, 2 * at.y - lastControl.y };
						}
						if (!r.pair(p)) break;
						p = rel(p);
						// A quadratic is a cubic whose controls sit two thirds of the way
						// out; raised here so nothing downstream has two kinds of curve.
						Segment seg;
						seg.curve = true;
						seg.c1 = { at.x + 2.0f / 3.0f * (q.x - at.x),
						           at.y + 2.0f / 3.0f * (q.y - at.y) };
						seg.c2 = { p.x + 2.0f / 3.0f * (q.x - p.x),
						           p.y + 2.0f / 3.0f * (q.y - p.y) };
						seg.to = p;
						current.segments.push_back(seg);
						lastControl = q;
						at = p;
						continue;
					} else if (upper == 'A') {
						float rx, ry, rot;
						bool large, sweep;
						Point p;
						if (!r.number(rx) || !r.number(ry) || !r.number(rot)) break;
						if (!r.flag(large) || !r.flag(sweep)) break;
						if (!r.pair(p)) break;
						p = rel(p);
						arcToCubics(at, p, rx, ry, rot, large, sweep, current.segments);
						at = p;
					} else {
						break;   // something this does not read; stop rather than guess
					}
					lastControl = at;
				}
				flush(false);
				return out;
			}

			// ---- flattening ------------------------------------------------------------
			void flattenCubic(Point a, Point b, Point c, Point d, float tolerance,
			                  std::vector<Point>& out, int depth = 0) {
				// Split until the controls are flat enough to be a line at this size.
				// Depth-limited, because a degenerate curve can be flat by this measure
				// and still not be finished.
				const float dx = d.x - a.x, dy = d.y - a.y;
				const float d1 = std::fabs((b.x - d.x) * dy - (b.y - d.y) * dx);
				const float d2 = std::fabs((c.x - d.x) * dy - (c.y - d.y) * dx);
				const float sum = d1 + d2;
				if (depth > 16 || sum * sum <= tolerance * (dx * dx + dy * dy)) {
					out.push_back(d);
					return;
				}
				const auto mid = [](Point p, Point q) {
					return Point{ (p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f };
				};
				const Point ab = mid(a, b), bc = mid(b, c), cd = mid(c, d);
				const Point abc = mid(ab, bc), bcd = mid(bc, cd);
				const Point abcd = mid(abc, bcd);
				flattenCubic(a, ab, abc, abcd, tolerance, out, depth + 1);
				flattenCubic(abcd, bcd, cd, d, tolerance, out, depth + 1);
			}

			std::vector<Point> flatten(const SubPath& path, const Matrix& toDevice,
			                           float tolerance) {
				std::vector<Point> out;
				Point at = toDevice.apply(path.start);
				out.push_back(at);
				for (const Segment& seg : path.segments) {
					const Point to = toDevice.apply(seg.to);
					if (!seg.curve) {
						out.push_back(to);
					} else {
						flattenCubic(at, toDevice.apply(seg.c1), toDevice.apply(seg.c2),
						             to, tolerance, out);
					}
					at = to;
				}
				return out;
			}

			// ---- the rasteriser ----------------------------------------------------------
			//
			// One routine, and everything is fed to it: a fill is the shape's contours, a
			// stroke is a quad per segment and a disc per join, and both are just polygons
			// with a winding rule. Doing it once means a stroked icon and a filled one
			// cannot disagree about what an edge looks like.
			struct Edge {
				float x0, y0, x1, y1;
				int   winding;
			};

			void addEdges(const std::vector<Point>& poly, std::vector<Edge>& edges) {
				for (size_t i = 0; i + 1 < poly.size(); ++i) {
					const Point a = poly[i];
					const Point b = poly[i + 1];
					if (a.y == b.y) continue;   // horizontal edges cross no scanline
					edges.push_back(a.y < b.y ? Edge{ a.x, a.y, b.x, b.y, 1 }
					                          : Edge{ b.x, b.y, a.x, a.y, -1 });
				}
				// Closed for the purposes of filling whether or not the path said so: an
				// unclosed fill is filled to its start, which is what every renderer does.
				if (poly.size() > 2) {
					const Point a = poly.back();
					const Point b = poly.front();
					if (a.y != b.y) {
						edges.push_back(a.y < b.y ? Edge{ a.x, a.y, b.x, b.y, 1 }
						                          : Edge{ b.x, b.y, a.x, a.y, -1 });
					}
				}
			}

			// Coverage for one shape, accumulated into `coverage` (width * height floats).
			void rasteriseEdges(const std::vector<Edge>& edges, bool evenOdd,
			                    uint32_t width, uint32_t height,
			                    std::vector<float>& coverage) {
				std::fill(coverage.begin(), coverage.end(), 0.0f);
				if (edges.empty()) return;

				struct Crossing { float x; int winding; };
				std::vector<Crossing> crossings;
				const float weight = 1.0f / static_cast<float>(kSubRows);

				for (uint32_t row = 0; row < height; ++row) {
					float* line = coverage.data() + static_cast<size_t>(row) * width;
					for (int sub = 0; sub < kSubRows; ++sub) {
						const float y = static_cast<float>(row) +
						                (static_cast<float>(sub) + 0.5f) * weight;
						crossings.clear();
						for (const Edge& e : edges) {
							if (y < e.y0 || y >= e.y1) continue;
							const float t = (y - e.y0) / (e.y1 - e.y0);
							crossings.push_back({ e.x0 + t * (e.x1 - e.x0), e.winding });
						}
						if (crossings.size() < 2) continue;
						std::sort(crossings.begin(), crossings.end(),
						          [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

						int winding = 0;
						for (size_t i = 0; i + 1 < crossings.size(); ++i) {
							winding += crossings[i].winding;
							const bool inside = evenOdd ? ((i & 1) == 0) : (winding != 0);
							if (!inside) continue;
							float from = crossings[i].x;
							float to = crossings[i + 1].x;
							if (to <= 0.0f || from >= static_cast<float>(width)) continue;
							from = (std::max)(from, 0.0f);
							to = (std::min)(to, static_cast<float>(width));
							if (to <= from) continue;

							// Exact coverage across the span: the partial pixel at each
							// end, and whole pixels between. Doing this rather than
							// sampling in x is what keeps a vertical edge crisp.
							const uint32_t first = static_cast<uint32_t>(from);
							const uint32_t lastPixel =
								(std::min)(static_cast<uint32_t>(to), width - 1);
							if (first == lastPixel) {
								line[first] += (to - from) * weight;
							} else {
								line[first] += (static_cast<float>(first + 1) - from) * weight;
								for (uint32_t x = first + 1; x < lastPixel; ++x) {
									line[x] += weight;
								}
								line[lastPixel] += (to - static_cast<float>(lastPixel)) * weight;
							}
						}
					}
				}
			}

			// Every piece of a stroke wound the same way.
			//
			// This is the whole of what makes a stroke a stroke rather than a row of
			// notches. The pieces overlap by construction -- a quad meets a disc at every
			// join -- and under the nonzero rule an overlap only fills if both pieces turn
			// the same way. Wound against each other they cancel, and the stroke comes out
			// with a bite taken from every corner. Which is exactly what it did.
			void addStrokePiece(std::vector<Point> poly, std::vector<Edge>& edges) {
				double twice = 0.0;
				for (size_t i = 0; i + 1 < poly.size(); ++i) {
					twice += static_cast<double>(poly[i].x) * poly[i + 1].y -
					         static_cast<double>(poly[i + 1].x) * poly[i].y;
				}
				if (twice > 0.0) std::reverse(poly.begin(), poly.end());
				addEdges(poly, edges);
			}

			// A stroke, as polygons. A quad per segment and a disc at every join and cap,
			// filled with the nonzero rule -- so the overlaps between them are the union
			// rather than a seam, which is exactly what a stroke is.
			void strokePolygons(const std::vector<Point>& line, float width, Cap cap,
			                    Join join, bool closed, std::vector<Edge>& edges) {
				const float half = width * 0.5f;
				if (half <= 0.0f || line.size() < 2) return;

				const auto disc = [&](Point centre) {
					// Enough sides that the roundness is not visible at this radius.
					const int sides = (std::max)(8, static_cast<int>(half * 2.0f) + 8);
					std::vector<Point> poly;
					poly.reserve(static_cast<size_t>(sides) + 1);
					for (int i = 0; i <= sides; ++i) {
						const float t = static_cast<float>(i) / sides * 2.0f * kPi;
						poly.push_back({ centre.x + std::cos(t) * half,
						                 centre.y + std::sin(t) * half });
					}
					addStrokePiece(std::move(poly), edges);
				};

				for (size_t i = 0; i + 1 < line.size(); ++i) {
					Point a = line[i], b = line[i + 1];
					float dx = b.x - a.x, dy = b.y - a.y;
					const float length = std::sqrt(dx * dx + dy * dy);
					if (length < 1e-6f) continue;
					dx /= length;
					dy /= length;
					if (cap == Cap::Square && !closed &&
					    (i == 0 || i + 2 == line.size())) {
						// Square caps are the segment run out by half a width at whichever
						// end is actually an end.
						if (i == 0)              { a.x -= dx * half; a.y -= dy * half; }
						if (i + 2 == line.size()) { b.x += dx * half; b.y += dy * half; }
					}
					const float nx = -dy * half, ny = dx * half;
					std::vector<Point> quad = {
						{ a.x + nx, a.y + ny }, { b.x + nx, b.y + ny },
						{ b.x - nx, b.y - ny }, { a.x - nx, a.y - ny },
						{ a.x + nx, a.y + ny },
					};
					addStrokePiece(std::move(quad), edges);
				}

				// Joins. A disc rounds them; a mitre on an icon at sixteen pixels is a
				// pixel of difference nobody can see, and a disc cannot spike the way a
				// mitre on a sharp angle does.
				//
				// Only where the line actually turns. A flattened curve is dozens of
				// segments that each bend by a fraction of a degree, and a disc at every
				// one of them is dozens of polygons to cover a wedge thinner than a pixel.
				for (size_t i = 1; i + 1 < line.size(); ++i) {
					const Point before = line[i - 1], at = line[i], after = line[i + 1];
					const float ax = at.x - before.x, ay = at.y - before.y;
					const float bx = after.x - at.x, by = after.y - at.y;
					const float la = std::sqrt(ax * ax + ay * ay);
					const float lb = std::sqrt(bx * bx + by * by);
					if (la < 1e-6f || lb < 1e-6f) continue;
					const float cosTurn = (ax * bx + ay * by) / (la * lb);
					// About eleven degrees: past that the wedge left between two quads is
					// wide enough to see at an icon's stroke width.
					if (cosTurn > 0.98f) continue;
					disc(at);
				}
				if (closed && line.size() > 2) disc(line.front());
				if (cap == Cap::Round && !closed) {
					disc(line.front());
					disc(line.back());
				}
				if (join == Join::Round && closed) disc(line.back());
			}

			// A polyline cut into its drawn pieces.
			//
			// This is the whole of what makes an icon draw itself: one dash as long as the
			// line, with the offset animated, is a stroke that slides into existence. It is
			// also the ordinary dashed border, which is the same mechanism standing still.
			std::vector<std::vector<Point>> dashed(const std::vector<Point>& line,
			                                       float dash, float gap, float offset) {
				std::vector<std::vector<Point>> out;
				if (dash <= 0.0f || line.size() < 2) {
					out.push_back(line);
					return out;
				}
				const float period = dash + (gap > 0.0f ? gap : dash);

				// Where in the pattern the line starts. A negative offset is how an icon is
				// written -- the dash begins off the end and slides on -- so it is wrapped
				// into the period rather than clamped away.
				float phase = std::fmod(offset, period);
				if (phase < 0.0f) phase += period;
				float along = phase;

				std::vector<Point> run;
				const auto on = [&](float at) { return std::fmod(at, period) < dash; };
				if (on(along)) run.push_back(line.front());

				for (size_t i = 0; i + 1 < line.size(); ++i) {
					const Point a = line[i], b = line[i + 1];
					const float dx = b.x - a.x, dy = b.y - a.y;
					const float length = std::sqrt(dx * dx + dy * dy);
					if (length < 1e-6f) continue;

					float walked = 0.0f;
					while (walked < length) {
						const float into = std::fmod(along, period);
						const bool drawing = into < dash;
						// To the end of this dash or gap, or the end of this segment.
						const float untilEdge = drawing ? (dash - into) : (period - into);
						const float step = (std::min)(untilEdge, length - walked);
						walked += step;
						along += step;
						const float t = walked / length;
						const Point at{ a.x + dx * t, a.y + dy * t };
						if (drawing) {
							run.push_back(at);
							if (walked < length || untilEdge <= step) {
								// The dash ended inside this segment: close the piece.
								if (std::fmod(along, period) >= dash || untilEdge <= step) {
									if (run.size() > 1) out.push_back(run);
									run.clear();
								}
							}
						} else if (std::fmod(along, period) < dash) {
							run.clear();
							run.push_back(at);
						}
					}
				}
				if (run.size() > 1) out.push_back(run);
				return out;
			}

			float srgbToLinear(float c) {
				return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
			}
			float linearToSrgb(float c) {
				return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
			}
		}

		// The document, once parsed.
		// A picture, and -- when it moves -- the document it was read from.
		//
		// A static icon is read once into shapes and the document thrown away. An animated
		// one is read *again* for every frame of its loop, because an animation is the same
		// tree evaluated at another moment.
		//
		// That would be a strange thing to do per frame, and it is not: the loop is
		// rasterised once, at load, into as many frames as it lasts. Re-reading is
		// therefore a cost paid thirty times when the icon appears and never again, which
		// buys a renderer with no separate animation model in it at all -- the same walk
		// answers "what does this look like", and the time is just another input.
		struct Picture {
			std::vector<Shape> shapes;
			float viewX = 0, viewY = 0, viewW = 0, viewH = 0;
			float width = 0, height = 0;
			float duration = 0.0f;     // seconds for one loop; zero if nothing moves
			std::string name;
			std::unique_ptr<tinyxml2::XMLDocument> doc;   // kept only when it moves
		};

		namespace {

			using tinyxml2::XMLElement;

			// Declared here because everything below reads attributes through it: the value
			// an animation says they have at this moment, or the one written on them.
			std::string resolved(const XMLElement* e, const char* name, float time);
			Matrix animatedTransform(const XMLElement* e, float time);

			float attr(const XMLElement* e, const char* name, float fallback, float time) {
				const std::string value = resolved(e, name, time);
				return value.empty() ? fallback : parseLength(value.c_str(), fallback);
			}

			void readPaint(const XMLElement* e, Inherited& style, float time) {
				const auto say = [&](const char* name) { return resolved(e, name, time); };

				if (const std::string fill = say("fill"); !fill.empty()) {
					style.hasFill = parsePaint(fill.c_str(), style.fill);
				}
				if (const std::string stroke = say("stroke"); !stroke.empty()) {
					style.hasStroke = parsePaint(stroke.c_str(), style.stroke);
				}
				if (const std::string w = say("stroke-width"); !w.empty()) {
					style.strokeWidth = parseLength(w.c_str(), style.strokeWidth);
				}
				if (const std::string o = say("opacity"); !o.empty()) {
					style.opacity *= parseLength(o.c_str(), 1.0f);
				}
				if (const std::string d = say("stroke-dasharray"); !d.empty()) {
					// One number is a dash and an equal gap, which is what a draw-itself
					// icon writes; two are the dash and the gap said separately.
					Reader r{ d.c_str(), d.c_str() + d.size() };
					float on = 0.0f, off = 0.0f;
					if (r.number(on)) {
						style.dash = on;
						style.gap = r.number(off) ? off : on;
					}
				}
				if (const std::string o = say("stroke-dashoffset"); !o.empty()) {
					style.dashOffset = parseLength(o.c_str(), 0.0f);
				}
				if (const char* r = e->Attribute("fill-rule")) {
					style.evenOdd = std::strncmp(r, "evenodd", 7) == 0;
				}
				if (const char* c = e->Attribute("stroke-linecap")) {
					style.cap = std::strncmp(c, "round", 5) == 0 ? Cap::Round
					          : std::strncmp(c, "square", 6) == 0 ? Cap::Square : Cap::Butt;
				}
				if (const char* j = e->Attribute("stroke-linejoin")) {
					style.join = std::strncmp(j, "round", 5) == 0 ? Join::Round
					           : std::strncmp(j, "bevel", 5) == 0 ? Join::Bevel : Join::Miter;
				}
				// The element's own transform, then whatever is turning it, then whatever
				// its parents were already doing.
				Matrix own;
				if (const char* t = e->Attribute("transform")) own = parseTransform(t);
				style.transform = animatedTransform(e, time).then(own).then(style.transform);
			}

			uint32_t withOpacity(uint32_t colour, float opacity, const char* own) {
				float alpha = static_cast<float>((colour >> 24) & 0xFFu) / 255.0f;
				alpha *= opacity;
				if (own) alpha *= parseLength(own, 1.0f);
				const int a = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
				return (colour & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
			}

			// A rectangle, including the rounded kind, as a path.
			std::vector<SubPath> rectPath(float x, float y, float w, float h,
			                              float rx, float ry) {
				if (rx <= 0.0f && ry <= 0.0f) {
					SubPath p;
					p.start = { x, y };
					p.segments = { Segment{ { x + w, y }, {}, {}, false },
					               Segment{ { x + w, y + h }, {}, {}, false },
					               Segment{ { x, y + h }, {}, {}, false } };
					p.closed = true;
					return { p };
				}
				if (rx <= 0.0f) rx = ry;
				if (ry <= 0.0f) ry = rx;
				rx = (std::min)(rx, w * 0.5f);
				ry = (std::min)(ry, h * 0.5f);
				SubPath p;
				p.start = { x + rx, y };
				p.segments.push_back(Segment{ { x + w - rx, y }, {}, {}, false });
				arcToCubics({ x + w - rx, y }, { x + w, y + ry }, rx, ry, 0, false, true,
				            p.segments);
				p.segments.push_back(Segment{ { x + w, y + h - ry }, {}, {}, false });
				arcToCubics({ x + w, y + h - ry }, { x + w - rx, y + h }, rx, ry, 0, false,
				            true, p.segments);
				p.segments.push_back(Segment{ { x + rx, y + h }, {}, {}, false });
				arcToCubics({ x + rx, y + h }, { x, y + h - ry }, rx, ry, 0, false, true,
				            p.segments);
				p.segments.push_back(Segment{ { x, y + ry }, {}, {}, false });
				arcToCubics({ x, y + ry }, { x + rx, y }, rx, ry, 0, false, true, p.segments);
				p.closed = true;
				return { p };
			}

			std::vector<SubPath> ellipsePath(float cx, float cy, float rx, float ry) {
				SubPath p;
				p.start = { cx + rx, cy };
				arcToCubics({ cx + rx, cy }, { cx - rx, cy }, rx, ry, 0, false, true, p.segments);
				arcToCubics({ cx - rx, cy }, { cx + rx, cy }, rx, ry, 0, false, true, p.segments);
				p.closed = true;
				return { p };
			}

			std::vector<SubPath> pointsPath(const char* text, bool close) {
				std::vector<SubPath> out;
				if (!text) return out;
				Reader r{ text, text + std::strlen(text) };
				SubPath p;
				Point first;
				if (!r.pair(first)) return out;
				p.start = first;
				Point next;
				while (r.pair(next)) p.segments.push_back(Segment{ next, {}, {}, false });
				p.closed = close;
				if (!p.segments.empty()) out.push_back(p);
				return out;
			}

			// ---- SMIL ------------------------------------------------------------------
			//
			// The animation an SVG carries inside itself: <animate> on an attribute,
			// <animateTransform> for turning and moving, <set> for a step change. Enough
			// of it for icons, which is what an icon set's exports use and nothing more.
			//
			// Everything is answered as a string, because that is what the rest of this
			// file already reads attributes as. An animation is therefore not a second
			// path through the renderer -- it is the same attribute with a different value.

			float seconds(const char* text, float fallback) {
				if (!text || !*text) return fallback;
				char* stop = nullptr;
				const float value = std::strtof(text, &stop);
				if (stop == text) return fallback;
				while (*stop == ' ') ++stop;
				if (std::strncmp(stop, "ms", 2) == 0) return value / 1000.0f;
				if (*stop == 'h') return value * 3600.0f;
				if (*stop == 'm' && stop[1] != 's') return value * 60.0f;
				return value;   // "s", or a bare number, which SMIL reads as seconds
			}

			// A list of numbers, which is what a transform's arguments and most animated
			// attributes are. Empty if it is not numbers at all -- a colour, a keyword.
			std::vector<float> numbers(const std::string& text) {
				std::vector<float> out;
				Reader r{ text.c_str(), text.c_str() + text.size() };
				float v = 0.0f;
				while (r.number(v)) out.push_back(v);
				return out;
			}

			std::vector<std::string> split(const std::string& text, char by) {
				std::vector<std::string> out;
				size_t at = 0;
				while (at <= text.size()) {
					const size_t next = text.find(by, at);
					const size_t stop = next == std::string::npos ? text.size() : next;
					out.push_back(text.substr(at, stop - at));
					if (next == std::string::npos) break;
					at = next + 1;
				}
				return out;
			}

			// Where in its own timeline an animation is at `time`, 0..1, and whether it has
			// anything to say yet. Repeats wrap; a finished one freezes or lets go,
			// depending on what it said.
			bool progress(const XMLElement* e, float time, float& out) {
				const float begin = seconds(e->Attribute("begin"), 0.0f);
				const float dur = seconds(e->Attribute("dur"), 0.0f);
				if (dur <= 0.0f) return false;
				float local = time - begin;
				if (local < 0.0f) return false;   // not started; the static value stands

				float repeats = 1.0f;
				const char* count = e->Attribute("repeatCount");
				const bool forever = count && std::strncmp(count, "indefinite", 10) == 0;
				if (count && !forever) repeats = std::strtof(count, nullptr);

				if (forever || local < dur * repeats) {
					local = std::fmod(local, dur);
				} else {
					// Over. `fill="freeze"` holds the last value, which is what a
					// draw-itself icon wants; anything else snaps back to the attribute.
					const char* fill = e->Attribute("fill");
					if (!fill || std::strncmp(fill, "freeze", 6) != 0) return false;
					out = 1.0f;
					return true;
				}
				out = dur > 0.0f ? local / dur : 0.0f;
				return true;
			}

			// The two values either side of `t`, and how far between them it is.
			bool endpoints(const XMLElement* e, float t, std::string& a, std::string& b,
			               float& mix, bool& discrete) {
				const char* mode = e->Attribute("calcMode");
				discrete = mode && std::strncmp(mode, "discrete", 8) == 0;

				if (const char* values = e->Attribute("values")) {
					std::vector<std::string> list = split(values, ';');
					if (list.empty()) return false;
					if (list.size() == 1) { a = b = list[0]; mix = 0.0f; return true; }

					// keyTimes says where each value sits; without it they are spread
					// evenly, which is what almost every exported icon relies on.
					std::vector<float> times;
					if (const char* keys = e->Attribute("keyTimes")) {
						for (const std::string& one : split(keys, ';')) {
							times.push_back(std::strtof(one.c_str(), nullptr));
						}
					}
					if (times.size() != list.size()) {
						times.clear();
						for (size_t i = 0; i < list.size(); ++i) {
							times.push_back(static_cast<float>(i) /
							                static_cast<float>(list.size() - 1));
						}
					}
					size_t at = 0;
					while (at + 2 < times.size() && t >= times[at + 1]) ++at;
					const float span = times[at + 1] - times[at];
					a = list[at];
					b = list[at + 1];
					mix = span > 0.0f ? (t - times[at]) / span : 0.0f;
					return true;
				}

				const char* from = e->Attribute("from");
				const char* to = e->Attribute("to");
				const char* by = e->Attribute("by");
				if (!to && !by) return false;
				a = from ? from : "0";
				if (to) {
					b = to;
				} else {
					// `by` is relative, so the far end is the near end plus it.
					const std::vector<float> base = numbers(a);
					const std::vector<float> step = numbers(by);
					std::string built;
					for (size_t i = 0; i < step.size(); ++i) {
						const float start = i < base.size() ? base[i] : 0.0f;
						built += (i ? " " : "") + std::to_string(start + step[i]);
					}
					b = built;
				}
				mix = t;
				return true;
			}

			// Two values blended. Numbers blend componentwise, colours per channel, and
			// anything else steps -- which is right, because there is no half way between
			// two keywords.
			std::string blend(const std::string& a, const std::string& b, float t,
			                  bool discrete) {
				if (discrete) return t < 1.0f ? a : b;
				const std::vector<float> from = numbers(a);
				const std::vector<float> to = numbers(b);
				if (!from.empty() && from.size() == to.size() &&
				    a.find('#') == std::string::npos && b.find('#') == std::string::npos) {
					std::string out;
					for (size_t i = 0; i < from.size(); ++i) {
						out += (i ? " " : "") +
						       std::to_string(from[i] + (to[i] - from[i]) * t);
					}
					return out;
				}
				uint32_t ca = 0, cb = 0;
				if (parsePaint(a.c_str(), ca) && parsePaint(b.c_str(), cb)) {
					uint32_t out = 0;
					for (int shift = 0; shift < 32; shift += 8) {
						const float x = static_cast<float>((ca >> shift) & 0xFFu);
						const float y = static_cast<float>((cb >> shift) & 0xFFu);
						const int v = static_cast<int>(x + (y - x) * t + 0.5f);
						out |= static_cast<uint32_t>(std::clamp(v, 0, 255)) << shift;
					}
					char text[16];
					std::snprintf(text, sizeof(text), "#%02X%02X%02X",
					              out & 0xFFu, (out >> 8) & 0xFFu, (out >> 16) & 0xFFu);
					return text;
				}
				return t < 0.5f ? a : b;
			}

			// The value of `name` on `e` at `time`: whatever is animating it, or the
			// attribute as written.
			std::string resolved(const XMLElement* e, const char* name, float time) {
				for (const XMLElement* anim = e->FirstChildElement(); anim;
				     anim = anim->NextSiblingElement()) {
					const char* tag = anim->Name();
					const bool set = std::strcmp(tag, "set") == 0;
					if (!set && std::strcmp(tag, "animate") != 0) continue;
					const char* target = anim->Attribute("attributeName");
					if (!target || std::strcmp(target, name) != 0) continue;

					if (set) {
						const float begin = seconds(anim->Attribute("begin"), 0.0f);
						if (time < begin) continue;
						const char* to = anim->Attribute("to");
						if (to) return to;
						continue;
					}
					float t = 0.0f;
					if (!progress(anim, time, t)) continue;
					std::string a, b;
					float mix = 0.0f;
					bool discrete = false;
					if (!endpoints(anim, t, a, b, mix, discrete)) continue;
					return blend(a, b, mix, discrete);
				}
				const char* written = e->Attribute(name);
				return written ? written : std::string();
			}

			// Every <animateTransform> on an element, composed, at `time`.
			Matrix animatedTransform(const XMLElement* e, float time) {
				Matrix out;
				for (const XMLElement* anim = e->FirstChildElement(); anim;
				     anim = anim->NextSiblingElement()) {
					if (std::strcmp(anim->Name(), "animateTransform") != 0) continue;
					float t = 0.0f;
					if (!progress(anim, time, t)) continue;
					std::string a, b;
					float mix = 0.0f;
					bool discrete = false;
					if (!endpoints(anim, t, a, b, mix, discrete)) continue;
					const std::string now = blend(a, b, mix, discrete);
					const char* type = anim->Attribute("type");
					const std::string kind = type ? type : "translate";
					// Built as the transform= syntax the parser already reads, rather than
					// a second way of spelling the same three operations.
					out = parseTransform((kind + "(" + now + ")").c_str()).then(out);
				}
				return out;
			}

			// How long the whole thing lasts: the latest moment anything is still moving.
			// An indefinite repeat is one cycle, because that is the loop to rasterise.
			void measure(const XMLElement* node, float& longest) {
				for (const XMLElement* e = node->FirstChildElement(); e;
				     e = e->NextSiblingElement()) {
					const char* tag = e->Name();
					if (std::strcmp(tag, "animate") == 0 ||
					    std::strcmp(tag, "animateTransform") == 0 ||
					    std::strcmp(tag, "animateMotion") == 0) {
						const float begin = seconds(e->Attribute("begin"), 0.0f);
						const float dur = seconds(e->Attribute("dur"), 0.0f);
						if (dur <= 0.0f) continue;
						float repeats = 1.0f;
						const char* count = e->Attribute("repeatCount");
						if (count && std::strncmp(count, "indefinite", 10) != 0) {
							repeats = std::strtof(count, nullptr);
						}
						longest = (std::max)(longest, begin + dur * (std::max)(1.0f, repeats));
					} else if (std::strcmp(tag, "set") == 0) {
						longest = (std::max)(longest, seconds(e->Attribute("begin"), 0.0f));
					}
					measure(e, longest);
				}
			}

			void walk(const XMLElement* node, Inherited style, Picture& picture, float time) {
				for (const XMLElement* e = node->FirstChildElement(); e;
				     e = e->NextSiblingElement()) {
					const char* tag = e->Name();
					// The animation elements themselves are read *by* the shapes they sit
					// inside, not drawn.
					if (std::strcmp(tag, "animate") == 0 ||
					    std::strcmp(tag, "animateTransform") == 0 ||
					    std::strcmp(tag, "animateMotion") == 0 ||
					    std::strcmp(tag, "set") == 0) {
						continue;
					}
					Inherited here = style;
					readPaint(e, here, time);

					if (std::strcmp(tag, "g") == 0) {
						walk(e, here, picture, time);
						continue;
					}

					std::vector<SubPath> paths;
					if (std::strcmp(tag, "path") == 0) {
						const std::string d = resolved(e, "d", time);
						paths = parsePath(d.c_str());
					} else if (std::strcmp(tag, "rect") == 0) {
						paths = rectPath(attr(e, "x", 0, time), attr(e, "y", 0, time),
						                 attr(e, "width", 0, time), attr(e, "height", 0, time),
						                 attr(e, "rx", 0, time), attr(e, "ry", 0, time));
					} else if (std::strcmp(tag, "circle") == 0) {
						const float r = attr(e, "r", 0, time);
						paths = ellipsePath(attr(e, "cx", 0, time), attr(e, "cy", 0, time), r, r);
					} else if (std::strcmp(tag, "ellipse") == 0) {
						paths = ellipsePath(attr(e, "cx", 0, time), attr(e, "cy", 0, time),
						                    attr(e, "rx", 0, time), attr(e, "ry", 0, time));
					} else if (std::strcmp(tag, "line") == 0) {
						SubPath p;
						p.start = { attr(e, "x1", 0, time), attr(e, "y1", 0, time) };
						p.segments.push_back(
							Segment{ { attr(e, "x2", 0, time), attr(e, "y2", 0, time) }, {}, {}, false });
						paths.push_back(p);
						here.hasFill = false;   // a line has no inside
					} else if (std::strcmp(tag, "polyline") == 0) {
						paths = pointsPath(e->Attribute("points"), false);
						if (!e->Attribute("fill")) here.hasFill = false;
					} else if (std::strcmp(tag, "polygon") == 0) {
						paths = pointsPath(e->Attribute("points"), true);
					} else {
						continue;   // text, defs, gradients: not an icon's business
					}
					if (paths.empty()) continue;

					// The transform is applied now, so the shape is in user space and
					// nothing downstream carries a stack.
					for (SubPath& p : paths) {
						p.start = here.transform.apply(p.start);
						for (Segment& s : p.segments) {
							s.to = here.transform.apply(s.to);
							if (s.curve) {
								s.c1 = here.transform.apply(s.c1);
								s.c2 = here.transform.apply(s.c2);
							}
						}
					}

					Shape shape;
					shape.paths = std::move(paths);
					shape.hasFill = here.hasFill;
					shape.fill = withOpacity(here.fill, here.opacity,
					                         e->Attribute("fill-opacity"));
					shape.evenOdd = here.evenOdd;
					shape.hasStroke = here.hasStroke;
					shape.stroke = withOpacity(here.stroke, here.opacity,
					                           e->Attribute("stroke-opacity"));
					shape.strokeWidth = here.strokeWidth * here.transform.scale();
					shape.cap = here.cap;
					shape.join = here.join;
					shape.dash = here.dash * here.transform.scale();
					shape.gap = here.gap * here.transform.scale();
					shape.dashOffset = here.dashOffset * here.transform.scale();
					picture.shapes.push_back(std::move(shape));
				}
			}
		}

		std::shared_ptr<Picture> parse(const std::string& xml, const std::string& named) {
			auto held = std::make_unique<tinyxml2::XMLDocument>();
			tinyxml2::XMLDocument& doc = *held;
			if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
				RDA_LOG_WARNING("svg '" << named << "': " << doc.ErrorStr());
				return nullptr;
			}
			const XMLElement* root = doc.RootElement();
			if (!root || std::strcmp(root->Name(), "svg") != 0) {
				RDA_LOG_WARNING("svg '" << named << "': the root element is not <svg>");
				return nullptr;
			}

			auto picture = std::make_shared<Picture>();
			picture->name = named;
			picture->width = attr(root, "width", 0.0f, 0.0f);
			picture->height = attr(root, "height", 0.0f, 0.0f);
			if (const char* box = root->Attribute("viewBox")) {
				Reader r{ box, box + std::strlen(box) };
				r.number(picture->viewX);
				r.number(picture->viewY);
				r.number(picture->viewW);
				r.number(picture->viewH);
			}
			// One of the two is enough: a document with only a viewBox is that size, and
			// one with only width and height has a viewBox of the same.
			if (picture->viewW <= 0.0f || picture->viewH <= 0.0f) {
				picture->viewW = picture->width;
				picture->viewH = picture->height;
			}
			if (picture->width <= 0.0f || picture->height <= 0.0f) {
				picture->width = picture->viewW;
				picture->height = picture->viewH;
			}
			if (picture->viewW <= 0.0f || picture->viewH <= 0.0f) {
				RDA_LOG_WARNING("svg '" << named << "': it states neither a size nor a "
				                           "viewBox, so there is no way to know how big it is");
				return nullptr;
			}

			// How long anything in it moves for. Zero means nothing does, and the document
			// can be let go the moment the shapes are out of it.
			measure(root, picture->duration);

			// The root carries paint too, and an icon set almost always puts it there --
			// fill="none" stroke="currentColor" stroke-width="2" on <svg> and nothing on
			// the paths. Reading it here is what makes a stroked icon stroked instead of a
			// solid blob of its own outline.
			Inherited base;
			readPaint(root, base, 0.0f);
			walk(root, base, *picture, 0.0f);
			// Kept only when something moves: a still icon has no reason to hold its own
			// source open for the life of the program.
			if (picture->duration > 0.0f) picture->doc = std::move(held);
			if (picture->shapes.empty()) {
				RDA_LOG_WARNING("svg '" << named << "': nothing in it is a shape this "
				                           "engine draws (paths, rects, circles, ellipses, "
				                           "lines, polylines and polygons are)");
				return nullptr;
			}
			return picture;
		}

		std::shared_ptr<Picture> load(const std::string& path) {
			tinyxml2::XMLDocument probe;
			if (probe.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
				RDA_LOG_WARNING("svg: cannot read " << path << ": " << probe.ErrorStr());
				return nullptr;
			}
			tinyxml2::XMLPrinter printer;
			probe.Print(&printer);
			return parse(std::string(printer.CStr(), printer.CStrSize() - 1), path);
		}

		bool flattenPath(const std::string& d, float tolerance, std::vector<float>& out) {
			out.clear();
			const std::vector<SubPath> paths = parsePath(d.c_str());
			if (paths.empty()) return false;
			// Every subpath end to end. A route written as two strokes is one journey with
			// a jump in it, which is a thing somebody may well want.
			for (const SubPath& path : paths) {
				const std::vector<Point> line = flatten(path, Matrix{}, tolerance);
				for (const Point& p : line) {
					out.push_back(p.x);
					out.push_back(p.y);
				}
			}
			return out.size() >= 4;
		}

		void size(const Picture& picture, float& width, float& height) {
			width = picture.width;
			height = picture.height;
		}

		float duration(const Picture& picture) { return picture.duration; }

		bool rasterise(const Picture& picture, uint32_t width, uint32_t height,
		               std::vector<unsigned char>& out, float time) {
			if (width == 0 || height == 0) return false;
			out.assign(static_cast<size_t>(width) * height * 4u, 0);

			// A moment other than the first means reading the tree again -- see the note on
			// Picture. A still icon, or the first frame of a moving one, is already here.
			std::vector<Shape> attheTime;
			const std::vector<Shape>* shapes = &picture.shapes;
			if (time > 0.0f && picture.doc && picture.doc->RootElement()) {
				Picture moment;
				moment.viewX = picture.viewX; moment.viewY = picture.viewY;
				moment.viewW = picture.viewW; moment.viewH = picture.viewH;
				Inherited base;
				readPaint(picture.doc->RootElement(), base, time);
				walk(picture.doc->RootElement(), base, moment, time);
				attheTime = std::move(moment.shapes);
				shapes = &attheTime;
			}

			// Fitted with its shape kept and centred, which is what preserveAspectRatio
			// defaults to and what an icon in a square box wants.
			const float scale = (std::min)(static_cast<float>(width) / picture.viewW,
			                               static_cast<float>(height) / picture.viewH);
			const float offsetX = (static_cast<float>(width) - picture.viewW * scale) * 0.5f;
			const float offsetY = (static_cast<float>(height) - picture.viewH * scale) * 0.5f;
			const Matrix toDevice{ scale, 0, 0, scale,
			                       offsetX - picture.viewX * scale,
			                       offsetY - picture.viewY * scale };

			// Half a pixel of tolerance, in device space -- finer than the eye at any icon
			// size, and coarse enough that a curve is a handful of lines rather than fifty.
			const float tolerance = 0.25f;

			std::vector<float> coverage(static_cast<size_t>(width) * height, 0.0f);
			std::vector<float> accum(static_cast<size_t>(width) * height * 4u, 0.0f);
			std::vector<Edge> edges;

			const auto composite = [&](uint32_t colour) {
				const float sa = static_cast<float>((colour >> 24) & 0xFFu) / 255.0f;
				if (sa <= 0.0f) return;
				// Composited in linear light: two translucent shapes over each other is
				// arithmetic on light, and doing it on sRGB values is the same mistake as
				// averaging them.
				const float sr = srgbToLinear(static_cast<float>(colour & 0xFFu) / 255.0f);
				const float sg = srgbToLinear(static_cast<float>((colour >> 8) & 0xFFu) / 255.0f);
				const float sb = srgbToLinear(static_cast<float>((colour >> 16) & 0xFFu) / 255.0f);
				for (size_t i = 0; i < coverage.size(); ++i) {
					const float a = std::clamp(coverage[i], 0.0f, 1.0f) * sa;
					if (a <= 0.0f) continue;
					float* p = accum.data() + i * 4u;
					p[0] = sr * a + p[0] * (1.0f - a);
					p[1] = sg * a + p[1] * (1.0f - a);
					p[2] = sb * a + p[2] * (1.0f - a);
					p[3] = a + p[3] * (1.0f - a);
				}
			};

			for (const Shape& shape : *shapes) {
				if (shape.hasFill) {
					edges.clear();
					for (const SubPath& path : shape.paths) {
						addEdges(flatten(path, toDevice, tolerance), edges);
					}
					rasteriseEdges(edges, shape.evenOdd, width, height, coverage);
					composite(shape.fill);
				}
				if (shape.hasStroke && shape.strokeWidth > 0.0f) {
					edges.clear();
					for (const SubPath& path : shape.paths) {
						std::vector<Point> line = flatten(path, toDevice, tolerance);
						if (path.closed && line.size() > 1) line.push_back(line.front());
						// Cut into dashes first, if it is dashed: each piece is then an
						// ordinary open stroke with its own caps.
						const std::vector<std::vector<Point>> pieces =
							dashed(line, shape.dash * scale, shape.gap * scale,
							       shape.dashOffset * scale);
						const bool whole = pieces.size() == 1 && shape.dash <= 0.0f;
						for (const std::vector<Point>& piece : pieces) {
							strokePolygons(piece, shape.strokeWidth * scale, shape.cap,
							               shape.join, whole && path.closed, edges);
						}
					}
					// Nonzero, always: the quads and discs a stroke is made of overlap, and
					// the union of them is the stroke. Even-odd would punch holes at every
					// join.
					rasteriseEdges(edges, false, width, height, coverage);
					composite(shape.stroke);
				}
			}

			for (size_t i = 0; i < coverage.size(); ++i) {
				const float* p = accum.data() + i * 4u;
				const float a = std::clamp(p[3], 0.0f, 1.0f);
				// Straight alpha out, and the colour encoded back to sRGB -- which is what
				// a sampled texture holds everywhere else in this engine.
				const auto channel = [&](float value) {
					const float unpremultiplied = a > 0.0001f ? value / a : 0.0f;
					return static_cast<unsigned char>(
						std::clamp(linearToSrgb(unpremultiplied), 0.0f, 1.0f) * 255.0f + 0.5f);
				};
				out[i * 4 + 0] = channel(p[0]);
				out[i * 4 + 1] = channel(p[1]);
				out[i * 4 + 2] = channel(p[2]);
				out[i * 4 + 3] = static_cast<unsigned char>(a * 255.0f + 0.5f);
			}
			return true;
		}
	}
}
