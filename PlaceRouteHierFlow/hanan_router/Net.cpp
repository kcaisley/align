#include <climits>
#include "Util.h"
#include "Placement.h"
#include <limits>

#include <algorithm>

namespace Placement {

void Net::print() const
{
  COUT << "pins :";
  for (const auto& p : _pins) {
    std::cout << " " << p->name();
  }
  if (!_ndrwidths.empty()) {
    COUT << " ndr :";
    for (const auto& lw : _ndrwidths) {
      COUT << "(layer : " << lw.first << " width : " << lw.second << ") ";
    }
  }
  if (!_ndrvias.empty()) {
    COUT << " ndr via :";
    for (const auto& lv : _ndrvias) {
      COUT << "(layer : " << lv.first << " width : " << lv.second.str() << ") ";
    }
  }
}

PortPairs Net::reorderPorts() const
{
  PortCVec ports;
  for (auto virt : {true, false}) {
    auto& pins = virt ? _vpins : _pins;
    for (auto& p : pins) {
      ports.insert(ports.end(), p->ports().begin(), p->ports().end());
    }
  }
  PortPairs porder;
  // two ports at least to route between: a net whose pins have fewer (a leaf's supply pin
  // without a shape, say) has nothing to connect here, and the pair search below needs two
  if (ports.size() < 2) {
    COUT << "net : " << _name << " has " << ports.size() << " port(s) on " << _pins.size() + _vpins.size()
         << " pin(s); nothing to route\n";
    return porder;
  }
  std::sort(ports.begin(), ports.end(),
      [](const Port* p1, const Port* p2) -> bool
      {
      const auto& b1 = p1->bbox();
      const auto& b2 = p2->bbox();
      if (b1.halfpm() == b2.halfpm()) {
      if (b1.ymin() == b2.ymin()) return b1.xmin() > b2.xmin();
      return b1.ymin() > b2.ymin();
      }
      return b1.halfpm() > b2.halfpm();
      }
      );
  std::vector< std::vector<double> > portpairdist(ports.size(), std::vector<double>(ports.size(), 0));
  // ports without shapes keep the 1e30 distance: the searches start above it, so every pair
  // is a candidate and the first pair stands when all are equal
  double mindist{std::numeric_limits<double>::infinity()};
  int idx1{0}, idx2{1};
  Geom::Rect netbbox;
  for (auto& p : ports) {
    netbbox.merge(p->bbox());
  }
  // ports all at one point: a unit half-perimeter keeps the distances finite (0/0 is NaN)
  double nethpwl{std::max((netbbox.width() + netbbox.height()) * 1., 1.)};
  for (unsigned i = 0; i < ports.size(); ++i) {
    auto& p1 = ports[i];
    auto& s1 = p1->shapes();
    for (unsigned j = i + 1; j < ports.size(); ++j) {
      auto& p2 = ports[j];
      double dist{1.e30};// = Geom::Dist(p1->bbox(), p2->bbox()) / nethpwl;
      auto& s2 = p2->shapes();
      for (auto& l1 : s1) {
        for (auto& r1 : l1.second) {
          for (auto& l2 : s2) {
            for (auto& r2 : l2.second) {
              dist = std::min(Geom::Dist(r1, r2)/nethpwl + std::abs(l1.first - l2.first) * 0.03, dist);
              /*if (ports[i]->isVirtualPort() || ports[j]->isVirtualPort()) {
                COUT << ports[i]->name() << ' ' << ports[j]->name() << ' ' <<  Geom::Dist(r1, r2) << ' ' << l1.first << ' ' << l2.first << ' ' << r1.str() << ' ' << r2.str() << ' ' << dist << '\n';
              }*/
            }
          }
        }
      }
      /*for (auto& l1 : s1) {
        auto its2 = s2.find(l1.first);
        if (its2 != s2.end()) {
          for (auto& r1 : l.second) {
            for (auto& r2 : its2->second) {
              dist = std::min(Geom::Dist(r1, r2)/nethpwl, dist);
            }
          }
        }
      }*/
      if (ports[i]->isVirtualPort() || ports[j]->isVirtualPort()) dist /= 10;
      portpairdist[i][j] = dist;
      portpairdist[j][i] = dist;
      COUT << "ports dist : " << ports[i]->name() << ' ' << ports[j]->name() << ' ' << dist << '\n';
      if (mindist > dist) {
        mindist = dist;
        idx1 = static_cast<int>(std::min(i, j));
        idx2 = static_cast<int>(std::max(i, j));
      } else if (mindist == dist) {
        if (idx1 > static_cast<int>(i)) {
          idx1 = static_cast<int>(std::min(i, j));
          idx2 = static_cast<int>(std::max(i, j));
        }
      }
    }
  }
  std::vector<std::pair<int, int>> primorder;
  primorder.reserve(ports.size() - 1);
  primorder.push_back(std::make_pair(idx1, idx2));
  COUT << "ports to route order : " << ports[idx1]->name() << ' ' << ports[idx2]->name() << '\n';
  std::vector<int> selected(ports.size(), 0);
  selected[idx1] = 1;
  selected[idx2] = 1;
  while (primorder.size() < ports.size() - 1) {
    double mindist{std::numeric_limits<double>::infinity()};
    int minidx2 = -1, minidx1 = -1;
    for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
      if (!selected[i]) continue;
      for (int j = 0; j < static_cast<int>(ports.size()); ++j) {
        if (!selected[j] && portpairdist[i][j] < mindist) {
          mindist = portpairdist[i][j];
          minidx1 = i;
          minidx2 = j;
        }
      }
    }
    if (minidx2 < 0) break;  // no unselected port left within reach: never spin
    primorder.push_back(std::make_pair(minidx1, minidx2));
    COUT << "ports to route order : " << ports[minidx1]->name() << ' ' << ports[minidx2]->name() << '\n';
    selected[minidx2] = 1;
  }

  std::sort(primorder.begin(), primorder.end(), [portpairdist](const std::pair<int, int>& a, const std::pair<int, int>& b) -> bool
      { return portpairdist[a.first][a.second] < portpairdist[b.first][b.second]; });

  
  for (auto& pp : primorder) porder.emplace_back(ports[pp.first], ports[pp.second]);

  return porder;
}

PortPairs Net::clockRouteOrder() const
{
  PortPairs porder;
  COUT << "clock net : " << _name << " with driver : " << _driver << '\n';
  const Pin* driver {nullptr};
  for (auto& p : _pins) {
    if (p->name() == _driver) {
      driver = p;
      break;
    }
  }
  if (driver != nullptr && !driver->ports().empty()) {
    for (unsigned j = 1; j < driver->ports().size(); ++j) {
      porder.push_back(std::make_pair(driver->ports()[0], driver->ports()[j]));
    }
    for (auto virt : {true, false}) {
      auto& pins = virt ? _vpins : _pins;
      for (auto& p : pins) {
        if (p == driver) continue;
        else {
          for (auto& port : p->ports()) {
            porder.push_back(std::make_pair(driver->ports()[0], port));
          }
        }
      }
    }
  }
  std::sort(porder.begin(), porder.end(),
      [](const PortPair& p1, const PortPair& p2) -> bool
      { return Geom::Dist(p1.first->bbox(), p1.second->bbox()) > Geom::Dist(p2.first->bbox(), p2.second->bbox()); }
      );
  for (auto& p : porder) {
    COUT << "ports to route order : " << p.first->name() << ' ' << p.second->name() << ' ' << Geom::Dist(p.first->bbox(), p.second->bbox()) << '\n';
  }
  return porder;
}

namespace {

// the area of a union of rectangles, by its x slabs (an island holds a few rectangles)
long long unionArea(const std::vector<Geom::Rect>& rs)
{
  std::vector<int> xs;
  xs.reserve(2 * rs.size());
  for (const auto& r : rs) {
    xs.push_back(r.xmin());
    xs.push_back(r.xmax());
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  long long area{0};
  std::vector<std::pair<int, int>> ys;
  for (size_t i = 0; i + 1 < xs.size(); ++i) {
    const int x0{xs[i]}, x1{xs[i + 1]};
    ys.clear();
    for (const auto& r : rs) {
      if (r.xmin() <= x0 && r.xmax() >= x1 && r.ymax() > r.ymin()) ys.emplace_back(r.ymin(), r.ymax());
    }
    std::sort(ys.begin(), ys.end());
    long long len{0};
    bool open{false};
    int y0{0}, y1{0};
    for (const auto& y : ys) {
      if (!open || y.first > y1) {
        if (open) len += y1 - y0;
        y0 = y.first;
        y1 = y.second;
        open = true;
      } else {
        y1 = std::max(y1, y.second);
      }
    }
    if (open) len += y1 - y0;
    area += len * (x1 - x0);
  }
  return area;
}

// closed: rectangles touching at an edge or a corner are one island
bool touching(const Geom::Rect& a, const Geom::Rect& b)
{
  return a.xmin() <= b.xmax() && b.xmin() <= a.xmax() && a.ymin() <= b.ymax() && b.ymin() <= a.ymax();
}

bool overlapping(const Geom::Rect& a, const Geom::Rect& b)
{
  return a.xmin() < b.xmax() && b.xmin() < a.xmax() && a.ymin() < b.ymax() && b.ymin() < a.ymax();
}

int floorTo(const int v, const int g)
{
  int q{v / g};
  if (v % g != 0 && v < 0) --q;
  return q * g;
}

int ceilTo(const int v, const int g) { return -floorTo(-v, g); }

long long lengthFor(const long long area, const long long width)
{
  if (area <= 0) return 0;
  return (width > 0) ? (area + width - 1) / width : LLONG_MAX;
}

}

void Net::growShortIslands(const HRouter::Router& router, const std::vector<const Geom::LayerRects*>& others, const std::string& modname)
{
  const int grid{std::max(1, router.mfgGrid())};
  for (auto& lr : _routeshapes) {
    const int z{lr.first};
    const long long minArea{router.minArea(z)};
    if (minArea <= 0 || lr.second.empty()) continue;
    // the net's metal on the layer: its pins and earlier routes, and this net's routes (flagged)
    std::vector<Geom::Rect> rects;
    std::vector<char> routed;
    auto itw = _routeshapeswithpins.find(z);
    if (itw != _routeshapeswithpins.end()) {
      for (const auto& r : itw->second) {
        if (r.width() > 0 && r.height() > 0) {
          rects.push_back(r);
          routed.push_back(0);
        }
      }
    }
    for (const auto& r : lr.second) {
      if (r.width() > 0 && r.height() > 0) {
        rects.push_back(r);
        routed.push_back(1);
      }
    }
    // its islands: rectangles that touch
    const size_t n{rects.size()};
    std::vector<int> island(n, -1);
    int islands{0};
    for (size_t i = 0; i < n; ++i) {
      if (island[i] >= 0) continue;
      std::vector<size_t> todo{i};
      island[i] = islands;
      while (!todo.empty()) {
        const auto k = todo.back();
        todo.pop_back();
        for (size_t j = 0; j < n; ++j) {
          if (island[j] < 0 && touching(rects[k], rects[j])) {
            island[j] = islands;
            todo.push_back(j);
          }
        }
      }
      ++islands;
    }
    const int space{router.metalSpace(z)}, ahead{router.endSpace(z)};
    std::vector<Geom::Rect> grown;  // metal added on the layer so far
    for (int c = 0; c < islands; ++c) {
      std::vector<Geom::Rect> shapes;
      bool hasroute{false};
      Geom::Rect bb;
      for (size_t i = 0; i < n; ++i) {
        if (island[i] != c) continue;
        if (shapes.empty()) bb = rects[i];
        else bb.merge(rects[i]);
        shapes.push_back(rects[i]);
        if (routed[i]) hasroute = true;
      }
      if (!hasroute) continue;  // a pin's own metal is its cell's
      const long long area{unionArea(shapes)};
      if (area >= minArea) continue;
      int dir{router.prefDir(z)};
      if (dir < 0) dir = (bb.height() >= bb.width()) ? 1 : 0;
      const bool vert{dir == 1};
      // the island's ends along the direction, each the extent across of the rectangles reaching it
      const int lo{vert ? bb.ymin() : bb.xmin()}, hi{vert ? bb.ymax() : bb.xmax()};
      int hiA0{INT_MAX}, hiA1{INT_MIN}, loA0{INT_MAX}, loA1{INT_MIN};
      for (const auto& r : shapes) {
        const int r0{vert ? r.ymin() : r.xmin()}, r1{vert ? r.ymax() : r.xmax()};
        const int a0{vert ? r.xmin() : r.ymin()}, a1{vert ? r.xmax() : r.ymax()};
        if (r1 == hi) {
          hiA0 = std::min(hiA0, a0);
          hiA1 = std::max(hiA1, a1);
        }
        if (r0 == lo) {
          loA0 = std::min(loA0, a0);
          loA1 = std::max(loA1, a1);
        }
      }
      const long long wHi{static_cast<long long>(hiA1) - hiA0}, wLo{static_cast<long long>(loA1) - loA0};
      const long long deficit{minArea - area};
      const long long longest{std::max(lengthFor(deficit, std::min(wHi, wLo)), static_cast<long long>(0))};
      if (longest == LLONG_MAX) continue;
      // the metal near it: the other nets', the unrouted pins, the instances' obstacles, the walls;
      // and this net's metal in its other islands (the grown end may join it, not come near it)
      const int reach{static_cast<int>(std::min<long long>(longest + 2 * grid + ahead + space, INT_MAX / 4))};
      const auto window = bb.bloatby(reach);
      std::vector<Geom::Rect> foreign, own;
      for (const auto* lrs : others) {
        auto it = lrs->find(z);
        if (it == lrs->end()) continue;
        for (const auto& r : it->second) {
          if (overlapping(r, window)) foreign.push_back(r);
        }
      }
      for (size_t i = 0; i < n; ++i) {
        if (island[i] != c && overlapping(rects[i], window)) own.push_back(rects[i]);
      }
      for (const auto& r : grown) {
        if (overlapping(r, window)) own.push_back(r);
      }
      // the lengths at the high and the low end: half the missing area at each, then all at one end,
      // then every split on the grid
      std::vector<std::pair<long long, long long>> splits;
      const long long half{(deficit + 1) / 2};
      splits.emplace_back(lengthFor(half, wHi), lengthFor(deficit - half, wLo));
      splits.emplace_back(lengthFor(deficit, wHi), 0);
      splits.emplace_back(0, lengthFor(deficit, wLo));
      for (long long k = grid, kmax = lengthFor(deficit, wHi); k < kmax && splits.size() < 10000; k += grid) {
        splits.emplace_back(k, lengthFor(deficit - k * wHi, wLo));
      }
      bool done{false};
      for (const auto& s : splits) {
        if (s.first == LLONG_MAX || s.second == LLONG_MAX) continue;
        const int newHi{s.first > 0 ? ceilTo(hi + static_cast<int>(s.first), grid) : hi};
        const int newLo{s.second > 0 ? floorTo(lo - static_cast<int>(s.second), grid) : lo};
        if (wHi * (newHi - hi) + wLo * (lo - newLo) < deficit) continue;
        // each added box, and the box no other metal may reach into: its space beside it, the
        // end-of-line space ahead of it
        std::vector<std::pair<Geom::Rect, Geom::Rect>> boxes;
        if (newHi > hi) {
          boxes.emplace_back(vert ? Geom::Rect(hiA0, hi, hiA1, newHi) : Geom::Rect(hi, hiA0, newHi, hiA1),
              vert ? Geom::Rect(hiA0 - space, hi, hiA1 + space, newHi + ahead) : Geom::Rect(hi, hiA0 - space, newHi + ahead, hiA1 + space));
        }
        if (newLo < lo) {
          boxes.emplace_back(vert ? Geom::Rect(loA0, newLo, loA1, lo) : Geom::Rect(newLo, loA0, lo, loA1),
              vert ? Geom::Rect(loA0 - space, newLo - ahead, loA1 + space, lo) : Geom::Rect(newLo - ahead, loA0 - space, lo, loA1 + space));
        }
        bool clear{true};
        for (const auto& b : boxes) {
          for (const auto& o : foreign) {
            if (overlapping(o, b.second)) {
              clear = false;
              break;
            }
          }
          for (const auto& o : own) {
            if (!clear) break;
            if (!touching(o, b.first) && overlapping(o, b.second)) clear = false;
          }
          if (!clear) break;
        }
        if (!clear) continue;
        for (const auto& b : boxes) {
          lr.second.push_back(b.first);
          _routeshapeswithpins[z].push_back(b.first);
          grown.push_back(b.first);
          COUT << "min area : " << modname << ' ' << _name << ' ' << LAYER_NAMES[z] << " island " << bb.str() << " area " << area << " < " << minArea << " : added " << b.first.str() << '\n';
        }
        done = true;
        break;
      }
      if (!done) {
        COUT << "min area : " << modname << ' ' << _name << ' ' << LAYER_NAMES[z] << " island " << bb.str() << " area " << area << " < " << minArea << " : no room to lengthen it\n";
      }
    }
  }
}

void Net::route(HRouter::Router& router, const Geom::LayerRects& l1, const Geom::LayerRects& l2, const Geom::LayerRects& l3, const bool update, const int uu, const Geom::Rect& bbox, const std::string& modname)
{
  //TIME_M();
#if DEBUG
  SaveRestoreStream src(_name + "_route.log");
#endif
  _unroute = 0;
  for (auto& pin : _pins) {
    for (auto& p : pin->ports()) {
      Geom::MergeLayerRects(_routeshapeswithpins, p->shapes(), &_bbox);
    }
  }
  if (router.debug()) {
    const Geom::LayerRects *obs[] = {&l1, &l2, &l3};
    writeLEF(modname, uu, bbox, obs);
  }
  if (_exclude) {
    COUT << "excluding net : " << _name << " from routing\n";
    return;
  }
  if (_pins.size() > 1) {
    COUT << "routing net : " << _name << ' ' << halfpm() << '\n';
    /*for (int i : {0, 1}) {
      for (auto& l : (i ? l1 : l2)) {
        for (auto& o : l.second) {
          COUT << "obs : " << l.first << ' ' << o.str() << '\n';
        }
      }
    }*/
    PortPairs ppairs = (_driver.empty() ? reorderPorts() : clockRouteOrder());
    for (auto& pp : ppairs) {
     // a pair the search finds no route for in the pins' box (and its margin) is tried again: where
     // a pin has shapes on more than one of the routing layers, first with each pin's lowest layer
     // alone (pin shapes a route cannot reach, as a capacitor's strip between its shield walls,
     // draw the search to them); then with the box widened as for a net allowed a large detour and
     // a longer search; before it is left open
     bool multilayer{false};
     for (int attempt = 0; attempt < 3; ++attempt) {
      if (attempt == 1 && !multilayer) continue;
      const bool lowestonly{attempt == 1 || (attempt == 2 && multilayer)};
      const auto& port1 = pp.first;
      const auto& port2 = pp.second;
      router.clearSourceTargets();
      COUT << "routing ports : " << port1->name() << ' ' << port2->name() << '\n';
      router.setName(_name + "__" + port1->name() + "__" + port2->name());
      const auto& p1 = port1->shapes();
      const auto& p2 = port2->shapes();
      bool preflayersrctgt{true};
      Geom::LayerRects samenetobst;
      for (auto src : {true, false}) {
        int lowest{INT_MAX}, nlayers{0};
        for (auto& l : (src ? p1 : p2)) {
          if (l.first > router.maxLayer() || l.first < router.minLayer() || l.second.empty()) continue;
          lowest = std::min(lowest, l.first);
          ++nlayers;
        }
        if (nlayers > 1) multilayer = true;
        bool preflayer{false};
        for (auto& l : _preflayers) {
          const auto& p = (src ? p1 : p2);
          auto it = p.find(l);
          if (it != p.end() && !it->second.empty()) {
            preflayer = true;
            break;
          }
        }
        preflayersrctgt &= preflayer;
        if (!_preflayers.empty()) {
          COUT << "pref layer pin" << (preflayer ? "" : " not") << " found for " << (src ? port1->name() : port2->name()) << '\n';
        }
        for (auto& l : (src ? p1 : p2)) {
          if (l.first > router.maxLayer() || l.first < router.minLayer()) {
            for (auto& s : l.second) samenetobst[l.first].push_back(s);
            continue;
          }
          if (preflayer && _preflayers.find(l.first) == _preflayers.end()) continue;
          if (lowestonly && l.first != lowest) continue;
          //COUT << "port : " << (src ? port1->name() : port2->name()) << " layer " << l.first << '\n';
          for (auto& s : l.second) {
            if (src) {
              router.addSourceShapes(s, l.first);
            } else {
              router.addTargetShapes(s, l.first);
            }
          }
        }
      }
      router.updatendr(update, _ndrwidths, _ndrspaces, _ndrdirs, _preflayers, _ndrvias);
#if DEBUG
      COUT << "adding line of sight nodes if they exist\n";
#endif
      for (auto& l : p1) {
        auto it = p2.find(l.first);
        if (preflayersrctgt && _preflayers.find(l.first) == _preflayers.end()) continue;
        if (it != p2.end()) {
          for (auto& s1 : l.second) {
            for (auto& s2 : it->second) {
              if (s1.xmin() < s2.xmax() && s1.xmax() > s2.xmin()) {
                int xmin(std::max(s1.xmin(), s2.xmin())), xmax(std::min(s1.xmax(), s2.xmax()));
                if (xmax - xmin >= router.widthy(l.first)) {
                  router.addSource(Geom::Rect(xmin, s1.ymin(), xmax, s1.ymax()), l.first);
                  router.addTarget(Geom::Rect(xmin, s2.ymin(), xmax, s2.ymax()), l.first);
                }
              } else if (s1.ymin() < s2.ymax() && s1.ymax() > s2.ymin()) {
                int ymin(std::max(s1.ymin(), s2.ymin())), ymax(std::min(s1.ymax(), s2.ymax()));
                if (ymax - ymin >= router.widthx(l.first)) {
                  router.addSource(Geom::Rect(s1.xmin(), ymin, s1.xmax(), ymax), l.first);
                  router.addTarget(Geom::Rect(s2.xmin(), ymin, s2.xmax(), ymax), l.first);
                }
              }
            }
          }
        }
      }
      if (_detour || attempt == 2) router.allowDetour();
      router.setMaxExpansions(attempt == 2 ? 100000 : 10000);  // the detour's wider window: a longer search
      router.addObstacles(l1, true);
      router.addObstacles(l2, true);
      router.addObstacles(l3, true);
      router.addObstacles(_obstacles, true);
      router.addObstacles(samenetobst, true);
      auto sol = router.findSol();
      if (sol.empty() && attempt < 2 && router.searched()) {
        COUT << (attempt == 0 && multilayer ? "retrying with the pins' lowest layers : " : "retrying with a large detour : ") << port1->name() << ' ' << port2->name() << '\n';
        router.clearObstacles(true);
        continue;
      }
      if (!sol.empty()) {
#if DEBUG
        for (auto& l : sol) {
          for (auto& s : l.second) {
            COUT << "sol : " << l.first << ' ' << s.str() << ' ' << s.width() << ' ' << s.height() << '\n';
          }
        }
#endif
        if (port1->isVirtualPort() || port2->isVirtualPort()) {
          for (auto i : {true, false}) {
            auto& port = i ? port1 : port2;
            if (port->isVirtualPort()) {
              auto& shapes = port->shapes();
              bool merged{false};
              for (auto& l : sol) {
                auto it = shapes.find(l.first);
                if (it != shapes.end()) {
                  for (auto& r : l.second) {
                    for (auto& rp : it->second) {
                      if (rp.overlaps(r)) {
                        if ((rp.ymin() == r.ymin() && rp.ymax() == r.ymax())
                            || (rp.xmin() == r.xmin() && rp.xmax() == r.xmax())) {
                          r.merge(rp);
                          merged = true;
                          break;
                        }
                      }
                    }
                  }
                  if (merged) break;
                }
                if (merged) break;
              }
            }
          }
        }
        Geom::MergeLayerRects(_routeshapeswithpins, sol, &_bbox);
        Geom::MergeLayerRects(_routeshapes, sol, &_bbox);
      } else {
        _unroute = 1;
      }
      if (!port1->isVirtualPort() || !_driver.empty()) {
        //std::cout << "Adding routes to " << port1->name() << ' ' << sol.size() << std::endl;
        Geom::MergeLayerRects(const_cast<Geom::LayerRects&>(port1->shapes()), sol, &_bbox);
        if (port2->isVirtualPort()) {
          Geom::MergeLayerRects(const_cast<Geom::LayerRects&>(port1->shapes()), port2->shapes(), &_bbox);
          Geom::MergeLayerRects(_routeshapeswithpins, port2->shapes(), &_bbox);
          Geom::MergeLayerRects(_routeshapes, port2->shapes(), &_bbox);
        }
      }
      if (!port2->isVirtualPort() || !_driver.empty()) {
        //std::cout << "Adding routes to " << port2->name() << ' ' << sol.size() << std::endl;
        Geom::MergeLayerRects(const_cast<Geom::LayerRects&>(port2->shapes()), sol, &_bbox);
        if (port1->isVirtualPort()) {
          Geom::MergeLayerRects(const_cast<Geom::LayerRects&>(port2->shapes()), port1->shapes(), &_bbox);
          Geom::MergeLayerRects(_routeshapeswithpins, port1->shapes(), &_bbox);
          Geom::MergeLayerRects(_routeshapes, port1->shapes(), &_bbox);
        }
      }
      router.clearObstacles(true);
      break;
     }
    }
    // the net routed: its islands below their metal's minimum area lengthened (the net's own metal,
    // its pins and earlier routes, counted; the other nets' metal, the unrouted pins, the instances'
    // obstacles and the module's walls kept clear)
    growShortIslands(router, {&l1, &l2, &l3, &_obstacles}, modname);
  }
}

void Net::writeLEF(const std::string& modname, const int uu, const Geom::Rect& bbox, const Geom::LayerRects* obs[]) const
{
  auto name(("net_" + modname + "_" + _name + ".lef"));
  COUT << "writing LEF file : " << name << "\n";
  std::ofstream ofs(name);
  if (ofs.is_open()) {
    ofs << "MACRO " << modname << "\n";
    ofs << "  UNITS\n    DISTANCE MICRONS " << uu << ";\n  END UNITS\n";
    ofs << "  ORIGIN "  << bbox.xmin()  << ' ' << bbox.ymin() << " ;\n";
    ofs << "  FOREIGN " << name << ' '  << (1.*bbox.xmin()/uu) << ' ' << (1.*bbox.ymin()/uu) << " ;\n";
    ofs << "  SIZE "    << (1.*bbox.width()/uu) << " BY " << (1.* bbox.height()/uu) << " ;\n";
    for (auto& v : {0, 1}) {
      for (auto& p : (v ? _pins : _vpins)) {
        ofs << "  PIN " << p->name() <<"\n    DIRECTION INOUT ;\n    USE SIGNAL ;\n";
        for (auto& pp : p->ports()) {
          ofs << "    PORT\n";
          for (auto& l : pp->shapes()) {
            ofs << "      LAYER " << LAYER_NAMES[l.first] << "_PIN ;\n";
            for (auto& r : l.second) {
              ofs << "        RECT " << (1.*r.xmin()/uu) << ' ' << (1.*r.ymin()/uu) << ' ' << (1.*r.xmax()/uu) << ' ' << (1.*r.ymax()/uu) << " ;\n";
            }
          }
          ofs << "      END\n";
        }
        ofs <<"  END " << p->name() << '\n';
      }
    }
    ofs << "  OBS\n";
    for (unsigned i = 0; i < 4; ++i) {
      if (obs[i]) {
        for (auto& l : (i < 3 ? *obs[i] : _obstacles)) {
          ofs << "      LAYER " << LAYER_NAMES[l.first] << " ;\n";
          for (auto r : l.second) {
            if (r.intersect(_bbox)) {
              ofs << "        RECT " << (1.*r.xmin()/uu) << ' ' << (1.*r.ymin()/uu) << ' ' << (1.*r.xmax()/uu) << ' ' << (1.*r.ymax()/uu) << " ;\n";
            }
          }
        }
      }
    }
    ofs << "    LAYER BBOX ;\n";
    ofs << "      RECT " << (1.*_bbox.xmin()/uu) << ' ' << (1.*_bbox.ymin()/uu) << ' ' << (1.*_bbox.xmax()/uu) << ' ' << (1.*_bbox.ymax()/uu) << " ;\n";
    ofs << "  END\n";
    ofs << "END " << modname << "\nEND LIBRARY\n";
  }
}

}
