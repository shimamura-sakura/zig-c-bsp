// Reference: https://hlbsp.sourceforge.net/index.php?content=bspdef
const std = @import("std");

comptime {
    if (@import("builtin").cpu.arch.endian() != .Little)
        @compileError("little-endian only");
}

/// bsp file header
/// only use as a pointer !
pub const Header = extern struct {
    const Self = @This();
    version: u32,
    entities: Lump,
    planes: Lump,
    textures: Lump,
    vertices: Lump,
    visibility: Lump,
    nodes: Lump,
    texinfos: Lump,
    faces: Lump,
    lighting: Lump,
    clipnodes: Lump,
    leaves: Lump,
    marksurfaces: Lump,
    edges: Lump,
    surfedges: Lump,
    models: Lump,
    pub fn fromBytes(bytes: anytype) *align(1) const Self {
        return @alignCast(@ptrCast(bytes));
    }
    pub fn getLumpArr(self: *align(1) const Self, lump: Lump, comptime T: type) []align(1) const T {
        const many: [*]align(1) const T = @ptrCast(self.getLumpPtr(lump, T));
        return many[0 .. lump.length / @sizeOf(T)];
    }
    pub fn getLumpPtr(self: *align(1) const Self, lump: Lump, comptime T: type) *align(1) const T {
        const bytes: [*]const u8 = @ptrCast(self);
        return @ptrCast(bytes + lump.offset);
    }
    pub fn getLumpBytes(self: *align(1) const Self, lump: Lump) []const u8 {
        const bytes: [*]const u8 = @ptrCast(self);
        return bytes[lump.offset..][0..lump.length];
    }
};

pub const Lump = extern struct {
    offset: u32,
    length: u32,
};

/// lump 0 entities: []u8
pub const EntityLump = @compileError("ascii text");

pub const vec3 = [3]f32;

/// lump 1 planes: []Plane
pub const Plane = extern struct {
    normal: vec3,
    dist: f32,
    type: i32,
};

/// lump 2 textures: TextureLump
/// only use as a pointer !
pub const TextureLump = extern struct {
    const Self = @This();
    count: u32,
    pub fn getOffsets(self: *align(1) const Self) []align(1) const u32 {
        const p: [*]const u8 = @ptrCast(self);
        const a: [*]align(1) const u32 = @ptrCast(p + @sizeOf(@This()));
        return a[0..self.count];
    }
    pub fn getMipTex(self: *align(1) const Self, off: u32) *align(1) const MipTex {
        const p: [*]const u8 = @ptrCast(self);
        return @ptrCast(p + off);
    }
};

/// only use as a pointer !
pub const MipTex = extern struct {
    _name: [16]u8,
    width: u32,
    height: u32,
    offsets: [4]u32,
    pub fn getName(self: *align(1) const @This()) []const u8 {
        if (std.mem.indexOfScalar(u8, &self._name, 0)) |i|
            return self._name[0..i];
        return &self._name;
    }
    pub fn getColors(self: *align(1) const @This()) *const [256][3]u8 {
        const miptex: [*]const u8 = @ptrCast(self);
        const offs_3 = self.offsets[3];
        const lens_3 = self.width * self.height >> 6;
        return @ptrCast(miptex + offs_3 + lens_3 + 2);
    }
    pub fn getTexture(self: *align(1) const @This(), level: u2) Texture {
        const miptex: [*]const u8 = @ptrCast(self);
        const width = self.width >> level;
        const height = self.height >> level;
        const offset = self.offsets[level];
        return .{
            .width = width,
            .height = height,
            .pixels = miptex[offset..][0 .. width * height],
        };
    }
};

pub const Texture = struct {
    width: u32,
    height: u32,
    pixels: []const u8,
};

/// lump 3 vertices: []vec3
pub const VertexLump = vec3;

/// lump 4 visibility
pub const VisLump = @compileError("see reference");

/// lump 5 nodes: []Node
pub const Node = extern struct {
    iPlane: u32,
    iChildren: [2]i16, // if neg: ~i is index into leaf
    mins: [3]i16,
    maxs: [3]i16,
    iFace0: u16,
    nFaces: u16,
};

/// lump 6 texinfo: []TexInfo
pub const TexInfo = extern struct {
    const Self = @This();
    vS: vec3,
    fSShift: f32,
    vT: vec3,
    fTShift: f32,
    iMipTex: u32,
    flags: u32,
    pub fn calcST(self: Self, vtx: vec3) [2]f32 {
        return .{
            vtx[0] * self.vS[0] + vtx[1] * self.vS[1] + vtx[2] * self.vS[2] + self.fSShift,
            vtx[0] * self.vT[0] + vtx[1] * self.vT[1] + vtx[2] * self.vT[2] + self.fTShift,
        };
    }
};

/// lump 7 faces: []Face
pub const Face = extern struct {
    iPlane: u16,
    iPSide: u16, // 0 - front, 1 - back
    iEdge0: u32, // surfedge
    nEdges: u16, // surfedge
    iTexInfo: u16,
    styles: [4]u8,
    lightmapOffset: u32,
};

/// lump 8 lighting
pub const LightmapLump = @compileError("array of rgb ([3]u8)");

/// lump 9 clipnodes: []ClipNode
pub const ClipNode = extern struct {
    iPlane: u32,
    iChildren: [2]i16, // if neg: content
};

pub const Contents = struct {
    pub const EMPTY = -1;
    pub const SOLID = -2;
    pub const WATER = -3;
    pub const SLIME = -4;
    pub const LAVA = -5;
    pub const SKY = -6;
    pub const ORIGIN = -7;
    pub const CLIP = -8;
    pub const CURRENT_0 = -9;
    pub const CURRENT_90 = -10;
    pub const CURRENT_180 = -11;
    pub const CURRENT_270 = -12;
    pub const CURRENT_UP = -13;
    pub const CURRENT_DOWN = -14;
    pub const TRANSLUCENT = -15;
};

/// lump 10 leaves
pub const Leaf = extern struct {
    contents: i32,
    visOffset: u32,
    mins: [3]i16,
    maxs: [3]i16,
    iMarkSurface0: u16,
    nMarkSurfaces: u16,
    ambientLevels: [4]u8,
};

/// lump 11 marksurfaces
pub const MarkSurfaces = @compileError("u16 index into face");

/// lump 12 edges
/// two indices into vertex
pub const Edge = [2]u16;

/// lump 13 edges: i32 index into edge, if neg: -i is index, reversed
pub const SurfEdge = i32;

/// lump 14 models
pub const Model = extern struct {
    mins: vec3,
    maxs: vec3,
    origin: vec3,
    iHeadnodes: [4]u32, // 0 - render, 123 - physics
    nVisLeafs: u32,
    iFace0: u32,
    nFaces: u32,
};
