/*
 * Copyright (C) 2002,2003,2004 Daniel Heck
 * Copyright (C) 2008 Ronald Lamprecht
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "errors.hh"
#include "laser.hh"
#include "SoundEffectManager.hh"
#include "stones_internal.hh"
#include "server.hh"
#include "stones/LaserStone.hh"
#include <algorithm>
#include <cassert>
#include <map>

using namespace std;

using ecl::V2;

namespace enigma {

/* -------------------- LaserBeam -------------------- */

    ItemTraits LaserBeam::traits = {"it-laserbeam", it_laserbeam, 
                                    itf_static | itf_indestructible, 0.0 };



/* -------------------- PhotoCell -------------------- */

vector<void*> PhotoCell::instances;

PhotoCell::~PhotoCell() 
{
    photo_deactivate();
}


/**
 * This function notifies all instances of PhotoCell that a
 * recalculation of the laser beams is about to begin by calling
 * on_recalc_start() for each instance.
 */
void PhotoCell::notify_start()
{
    for(unsigned i=0; i<instances.size(); ++i)
    {
        PhotoCell *pc = (PhotoCell*) instances[i];
        pc->on_recalc_start();
    }
}

/**
 * This function notifies all instances of PhotoCell that the engine
 * has finished recalculating the laser beams by calling
 * on_recalc_finish() for each instance.
 */
void PhotoCell::notify_finish()
{
    for(unsigned i=0; i<instances.size(); ++i)
    {
        PhotoCell *pc = (PhotoCell*) instances[i];
        pc->on_recalc_finish();
    }
}

void PhotoCell::photo_activate()
{
    vector<void*>::iterator i = std::find(instances.begin(), instances.end(), this);
    if (i != instances.end())
        assert (0 || "Photocell activated twice\n");
    else
        instances.push_back (this);
}

void PhotoCell::photo_deactivate()
{
    vector<void*>::iterator i;
    i = std::find (instances.begin(), instances.end(), this);
    if (i != instances.end())
        instances.erase(i);
}


/* -------------------- PhotoStone -------------------- */

PhotoStone::PhotoStone(const char *kind) : Stone(kind) 
{
    illuminated = false;
}

PhotoStone::PhotoStone()
{
    illuminated = false;
}

void PhotoStone::on_recalc_start() 
{}

void PhotoStone::on_recalc_finish() 
{ 
    GridPos p = get_pos();
    bool illu = (LightFrom(p, NORTH) || LightFrom(p, EAST)
                 || LightFrom(p, WEST) || LightFrom(p, SOUTH));

    if (illu != illuminated) {
        if (illu) notify_laseron();
        else      notify_laseroff();
        illuminated = illu;
    }
}


/* -------------------- LaserBeam -------------------- */

// The implementation of laser beams is a little tricky because, in
// spite of being implemented as Items, lasers aren't localized and
// changes to any part of the beam can affect the beam elsewhere.  A
// `change' may be anything from moving a stone in or out of the beam,
// rotating or moving one of the mirrors, to making a stone in the
// beam transparent.
//
// Here are a couple of facts about laser beams in Enigma:
//
// - Laser beams are static. Once calculated they do not change until
//   they are completely recalculated
//
// - LaserBeam::emit_from() is the only way to emit laser beams. A new
//   beam will propagate automatically and stops only if it comes
//   across an item or a stone that returns `false' from
//   Stone::is_transparent().
//
// - `processLight()' is called for objects in the beam *whenever*
//   the beam is recalculated.  For objects that need to be notified
//   when the laser goes on or off, use the `PhotoStone'
//   mixin.

std::list<LaserBeam *> LaserBeam::beamList;


void LaserBeam::prepareLevel() {
    beamList.clear();
}

void LaserBeam::init_model()
{
    DirectionBits directions = (DirectionBits)(objFlags & 15);
//    Log << "LaserBeam init model " << directions << " - " << objFlags << "\n";
    
    if (directions & (EASTBIT | WESTBIT)) {
        if (directions & (NORTHBIT | SOUTHBIT))
            set_model("it-laserhv");
        else
            set_model("it-laserh");
    }
    else if (directions & (NORTHBIT | SOUTHBIT))
        set_model("it-laserv");
}

void LaserBeam::processLight(Direction dir) {
    DirectionBits dirbit = to_bits(dir);
    
    if ((objFlags & 15 & dirbit) == 0) {
        // new direction
        objFlags |= dirbit;
        emit_from(get_pos(), dir);        
    }
}

void LaserBeam::on_creation(GridPos p)
{
    beamList.push_back(this);
    DirectionBits directions = (DirectionBits)(objFlags & 15);
    
    if (directions & EASTBIT) emit_from(p, EAST);
    else if (directions & WESTBIT) emit_from(p, WEST);
    else if (directions & NORTHBIT) emit_from(p, NORTH);
    else if (directions & SOUTHBIT) emit_from(p, SOUTH);
}

void LaserBeam::on_removal(GridPos p) {
    if ((objFlags & 15) != 0) {    // extraordinary removal of a laser beam
        beamList.erase(find(beamList.begin(), beamList.end(), this));
    }
    Item::on_removal(p);
}

void LaserBeam::emit_from(GridPos p, Direction dir)
{
    bool may_pass = true;

    p.move(dir);
    if (Stone *st = GetStone(p)) {
        may_pass = st->is_transparent (dir);
        st->processLight(dir);
    }

    if (may_pass) {
        if (Item *it = GetItem(p))
            it->processLight(dir);
        else {
            LaserBeam *lb = new LaserBeam(dir);
            SetItem(p, lb);
        }
    }
}

bool LaserBeam::actor_hit(Actor *actor)
{
    DirectionBits directions = (DirectionBits)(objFlags & 15);

    double r = get_radius(actor);
    V2 p = actor->get_pos();
    GridPos gp = get_pos();

    // distance of actor from center of the grid
    double dx = fabs(p[0] - gp.x - 0.5) - r;
    double dy = fabs(p[1] - gp.y - 0.5) - r;

    if ((directions & (EASTBIT | WESTBIT) && dy<-0.1) ||
        (directions & (NORTHBIT | SOUTHBIT)) && dx<-0.1)
    {
        SendMessage(actor, "laserhit");
    }

    return false; // laser beams can't be picked up
}

void LaserBeam::kill_all()
{    
    
    for (list<LaserBeam *>::iterator itr = beamList.begin(); itr != beamList.end(); ++itr) {
        uint32_t flags = (*itr)->objFlags;
        (*itr)->objFlags = (flags & ~255) | ((flags & 15) << 4);  // remember last laser bits, clear current ones
    }
}

void LaserBeam::all_emitted()
{
    double x = 0, y = 0;
    int    count = 0;
    
    for (list<LaserBeam *>::iterator itr = beamList.begin(); itr != beamList.end(); ) {
        list<LaserBeam *>::iterator witr = itr;  // work iterator for possible deletion of object
        ++itr;                                   // main iterator does no longer point to critical object
        uint32_t flags = (*witr)->objFlags;
//        Log << "LaserBeam allemit px " << (*witr)->get_pos().x << " y " << (*witr)->get_pos().y << " f " << flags << "\n";
        uint32_t newDirs = flags & 15;
        if (newDirs == 0) {
            // this grid is now free of laserbeam
            KillItem((*witr)->get_pos());
            beamList.erase(witr);
        } else if ((flags & 240) != (newDirs << 4)) {
            // the laser beam on grid changed or is new
            (*witr)->init_model();
            
            // sound position calculation
            if ((((flags & 240) >> 4) & newDirs) != newDirs) {
                // a beam has been added here
                GridPos p = (*witr)->getOwnerPos();
                x += p.x;
                y += p.y;
                ++count;
            } 
        }
    }

    if (count) {
        sound::EmitSoundEvent ("laseron", ecl::V2(x/count+.5, y/count+.5),
                               getVolume("laseron", NULL));
    }
}

void LaserBeam::dispose()
{
    delete this;
}


/* -------------------- MirrorStone -------------------- */
namespace
{
    class MirrorStone
        : public Stone, public PhotoCell
    {
    protected:
        MirrorStone(const char *name, bool movable=false, bool transparent=false);

        bool is_transparent() const { return getAttr("transparent") != 0; }

        void set_orientation(int o) { setAttr("orientation", o); }
        int get_orientation() { return getAttr("orientation"); }

        void emit_light(Direction dir) {
            if (!has_dir(outdirs, dir))
            {
                outdirs = DirectionBits(outdirs | to_bits(dir));
                LaserBeam::emit_from(get_pos(), dir);
            }
        }

        void init_model();
        void setAttr(const string& key, const Value &val);

    private:

        StoneTraits traits;

	// Object interface.
        virtual Value message(const Message &m);

        //GridObject interface
        DirectionBits emissionDirections() const {
            return outdirs;
        }

        // PhotoCell interface
        void on_recalc_start() { outdirs = NODIRBIT; }
        void on_recalc_finish() {}

        // Stone interface
        void actor_hit(const StoneContact &sc);
        void on_creation (GridPos p);
        void on_removal (GridPos p);
        bool is_transparent(Direction) const { return is_transparent(); }

        // Private methods
        void rotate_right();
        
        const StoneTraits &get_traits() const { return traits; }
        
        // Variables
        DirectionBits outdirs;    
    };
}

MirrorStone::MirrorStone(const char *name, bool movable, bool transparent)
: Stone(name), outdirs(NODIRBIT)
{
    setAttr("transparent", transparent);
    setAttr("movable", movable);
    setAttr("orientation", Value(1));
    traits.name = "st-mirror";
    traits.id = st_INVALID;
    traits.flags = stf_none;
    traits.material = material_stone;
    traits.restitution = 1.0;
    //traits.movable is already set via setAttr("movable", ...);
}

void MirrorStone::init_model() {
    string mname = get_kind();
    mname += is_movable() ? "-m" : "-s";
    mname += is_transparent() ? "t" : "o";
    mname += char('0' + get_orientation());
    set_model(mname);
}

Value MirrorStone::message(const Message &m) {
    if ((m.message == "_trigger" && m.value.to_bool()) || m.message == "turn") {
        rotate_right();
        return Value();
    }
    else if (m.message == "signal") {
        if (to_double(m.value) != 0) {
            rotate_right();
        }
        return Value();
    }
    else if (m.message == "mirror-north") {
        set_orientation(3);
        init_model();
        MaybeRecalcLight(get_pos());
        return Value();
    }
    else if (m.message == "mirror-east") {
        set_orientation(4);
        init_model();
        MaybeRecalcLight(get_pos());
        return Value();
    }
    else if (m.message == "mirror-south") {
        set_orientation(1);
        init_model();
        MaybeRecalcLight(get_pos());
        return Value();
    }
    else if (m.message == "mirror-west") {
        set_orientation(2);
        init_model();
        MaybeRecalcLight(get_pos());
        return Value();
    }
    return Stone::message(m);
}

void MirrorStone::actor_hit(const StoneContact &sc)
{
    if (is_movable())
        maybe_push_stone(sc);
    rotate_right();
}

void MirrorStone::on_creation (GridPos p)
{
    photo_activate();
    Stone::on_creation(p);
}

void MirrorStone::on_removal(GridPos p)
{
    photo_deactivate();
    Stone::on_removal(p);
}

void MirrorStone::rotate_right()
{
    set_orientation(1+(get_orientation() % 4));
    init_model();
    MaybeRecalcLight(get_pos());
    sound_event ("mirrorturn");
}

void MirrorStone::setAttr(const string& key, const Value &val) {
    Stone::setAttr(key, val);
    if (key == "movable")
        traits.movable = to_bool(val) ? MOVABLE_STANDARD : MOVABLE_PERSISTENT;
}
    

/* -------------------- Plane Mirror -------------------- */
namespace
{
    class PlaneMirror : public MirrorStone {
        CLONEOBJ(PlaneMirror);
    public:
        PlaneMirror(char orientation='/', bool movable=false, bool transparent=false)
        : MirrorStone("st-pmirror", movable, transparent)
        {
            SetOrientation(orientation);
        }
    private:
        void SetOrientation(char o) {
            const char *a = " -\\|/";
            MirrorStone::set_orientation(int (strchr(a,o)-a));
        }
        char GetOrientation() {
            const char *a = " -\\|/";
            return a[MirrorStone::get_orientation()];
        }
        void processLight(Direction dir);
    };
}

void PlaneMirror::processLight(Direction dir) 
{
    char orientation = GetOrientation();
    bool transparent = is_transparent();

    switch (orientation) {
    case '|':
        if (dir==EAST || dir==WEST) {
            emit_light(reverse(dir));
            if (transparent)
                emit_light(dir);
        }
        else if ((dir == NORTH || dir == SOUTH) && transparent &&
                 server::GameCompatibility == GAMET_OXYD1) {
            emit_light(dir);
        }
        break;
    case '-':
        if (dir==NORTH || dir==SOUTH) {
            emit_light(reverse(dir));
            if (transparent)
                emit_light(dir);
        }
        else if ((dir == EAST || dir == WEST) && transparent &&
                 server::GameCompatibility == GAMET_OXYD1) {
            emit_light(dir);
        }
        break;
    case '/':
        switch(dir) {
        case EAST: emit_light(NORTH); break;
        case SOUTH: emit_light(WEST); break;
        case NORTH: emit_light(EAST); break;
        case WEST: emit_light(SOUTH); break;
        case NODIR: break;
        }
        if (transparent)
            emit_light(dir);
        break;
    case '\\':
        switch(dir) {
        case EAST: emit_light(SOUTH); break;
        case SOUTH: emit_light(EAST); break;
        case NORTH: emit_light(WEST); break;
        case WEST: emit_light(NORTH); break;
        case NODIR: break;
        }
        if (transparent)
            emit_light(dir);
        break;
    }
}


/* -------------------- TriangleMirror -------------------- */

namespace
{
    // The orientations of the TriangleMirror have an unusual definition,
    // but we cannot change them w/o changing many levels
    //
    //    Flat side of the triangle
    //    points to :                            Orientation :
    //
    //    WEST                                   4
    //    SOUTH                                  3
    //    EAST                                   2
    //    NORTH                                  1

    class TriangleMirror : public MirrorStone {
        CLONEOBJ(TriangleMirror);
    public:
        TriangleMirror(char orientation='v', bool movable=false, bool transparent=false)
        : MirrorStone("st-3mirror", movable, transparent)
        {
            SetOrientation (orientation);
        }
    private:

        void SetOrientation(char o) {
            const char *a = " v<^>";
            MirrorStone::set_orientation( int (strchr(a,o)-a));
        }

        Direction GetOrientation() // orientation of the flat side of the mirror
        {
            const Direction a[] = {NODIR, NORTH, EAST, SOUTH, WEST};
            return a[MirrorStone::get_orientation()];
        }
        void processLight(Direction dir);
    };
}

void TriangleMirror::processLight(Direction beam_dir)
    // note: 'beam_dir' is the direction where laserbeam goes to
{
    // direction where flat side of triangle points to
    Direction flat_dir = GetOrientation();
    Direction tip_dir  = reverse(flat_dir);

    if (beam_dir == tip_dir) // beam hits the flat side
        emit_light(flat_dir);
    else if (beam_dir == flat_dir) {
        // this is the "complicated" case where the light falls
        // on the tip of the triangle
        switch (beam_dir) {
            case SOUTH: case NORTH:
                emit_light(EAST); emit_light(WEST); break;
            case WEST: case EAST:
                emit_light(SOUTH); emit_light(NORTH); break;
            case NODIR: break;
        }
    } else
        emit_light(tip_dir);

    if (is_transparent())
        emit_light(beam_dir);
}

//----------------------------------------------------------------------
// FUNCTIONS
//----------------------------------------------------------------------
namespace
{
    /* This flag is true iff all lasers should be recalculated at the
       end of the next tick. */
    bool light_recalc_scheduled = false;
}

void InitLasers() {
//    Register (new LaserStone);
//    Register ("st-laser-n", new LaserStone(NORTH));
//    Register ("st-laser-e", new LaserStone(EAST));
//    Register ("st-laser-s", new LaserStone(SOUTH));
//    Register ("st-laser-w", new LaserStone(WEST));

    Register (new TriangleMirror);
    Register ("st-mirror-3v", new TriangleMirror('v'));
    Register ("st-mirror-3<", new TriangleMirror('<'));
    Register ("st-mirror-3^", new TriangleMirror('^'));
    Register ("st-mirror-3>", new TriangleMirror('>'));
    Register ("st-mirror-3vm", new TriangleMirror('v', true));
    Register ("st-mirror-3<m", new TriangleMirror('<', true));
    Register ("st-mirror-3^m", new TriangleMirror('^', true));
    Register ("st-mirror-3>m", new TriangleMirror('>', true));
    Register ("st-mirror-3vt", new TriangleMirror('v', false, true));
    Register ("st-mirror-3<t", new TriangleMirror('<', false, true));
    Register ("st-mirror-3^t", new TriangleMirror('^', false, true));
    Register ("st-mirror-3>t", new TriangleMirror('>', false, true));
    Register ("st-mirror-3vtm", new TriangleMirror('v', true, true));
    Register ("st-mirror-3<tm", new TriangleMirror('<', true, true));
    Register ("st-mirror-3^tm", new TriangleMirror('^', true, true));
    Register ("st-mirror-3>tm", new TriangleMirror('>', true, true));

    Register (new PlaneMirror);
    Register ("st-mirror-p|", new PlaneMirror('|'));
    Register ("st-mirror-p/", new PlaneMirror('/'));
    Register ("st-mirror-p-", new PlaneMirror('-'));
    Register ("st-mirror-p\\", new PlaneMirror('\\'));
    Register ("st-mirror-p|m", new PlaneMirror('|', true));
    Register ("st-mirror-p/m", new PlaneMirror('/', true));
    Register ("st-mirror-p-m", new PlaneMirror('-', true));
    Register ("st-mirror-p\\m", new PlaneMirror('\\', true));
    Register ("st-mirror-p|t", new PlaneMirror('|', false, true));
    Register ("st-mirror-p/t", new PlaneMirror('/', false, true));
    Register ("st-mirror-p-t", new PlaneMirror('-', false, true));
    Register ("st-mirror-p\\t", new PlaneMirror('\\', false, true));
    Register ("st-mirror-p|tm", new PlaneMirror('|', true, true));
    Register ("st-mirror-p/tm", new PlaneMirror('/', true, true));
    Register ("st-mirror-p-tm", new PlaneMirror('-', true, true));
    Register ("st-mirror-p\\tm", new PlaneMirror('\\', true, true));
}


void MaybeRecalcLight(GridPos p) {
    light_recalc_scheduled |=
        (LightFrom(p, NORTH) || LightFrom(p, SOUTH) ||
         LightFrom(p, WEST) || LightFrom(p, EAST));
}

void RecalcLight() {
    light_recalc_scheduled = true;
}

bool LightFrom (GridPos p, Direction dir) {
    p.move(dir);
    if (GridObject *obj = GetStone(p)) 
        if (has_dir(obj->emissionDirections(), reverse(dir)))
            return true;
    if (GridObject *obj = GetItem(p))
        return (has_dir(obj->emissionDirections(), reverse(dir)));
    
    return false;
}

void PerformRecalcLight(bool isInit) {
    if (light_recalc_scheduled) {
        light_recalc_scheduled = false;    // this is the right place - but we have first to fix some object like hammer,...
        PhotoCell::notify_start();
        GridObject::preLaserRecalc();
        LaserBeam::kill_all();
        LaserStone::reemit_all();
        LaserBeam::all_emitted();
        if (!isInit)
            // do not cause actions on initial laser beam generation
            GridObject::postLaserRecalc();
        PhotoCell::notify_finish();
//        light_recalc_scheduled = false;
    }
}

} // namespace enigma
