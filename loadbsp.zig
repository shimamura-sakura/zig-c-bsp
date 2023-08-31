const std = @import("std");
const bsp = @import("hlbsp.zig");
const alloc = std.heap.c_allocator;

const ZigLoadBSP = extern struct {
    vbo_data: [*]u8,
    vbo_size: usize,
    ebo_data: [*]u8,
    ebo_size: usize,
    textures: [*]ZigBSPTex,
    text_cnt: usize,
    clipnode: [*]bsp.ClipNode,
    clip_cnt: usize,
    world_h1: i32,
    world_h2: i32,
    world_h3: i32,
    b_planes: [*]bsp.Plane,
    planecnt: usize,
};

const ZigBSPTex = extern struct {
    width: u32,
    height: u32,
    i3Index0: u32,
    n3Indexs: u32,
    pixels: ?[*][4]u8,
    skipped: u8,
};

export fn zigLoadBSP(
    i_filename: [*:0]const u8,
    o_ldresult: *ZigLoadBSP,
) i32 {
    if (loadBSP(i_filename)) |result| {
        o_ldresult.* = result;
        return 0;
    } else |_| {
        return -1;
    }
}

export fn zigFreeBSP(
    i_ldresult: *ZigLoadBSP,
) void {
    alloc.free(i_ldresult.vbo_data[0..i_ldresult.vbo_size]);
    alloc.free(i_ldresult.ebo_data[0..i_ldresult.ebo_size]);
    const textures = i_ldresult.textures[0..i_ldresult.text_cnt];
    for (textures) |t| if (t.pixels) |p| alloc.free(p[0 .. t.width * t.height]);
    alloc.free(textures);
    alloc.free(i_ldresult.clipnode[0..i_ldresult.clip_cnt]);
    alloc.free(i_ldresult.b_planes[0..i_ldresult.planecnt]);
}

fn loadBSP(i_filename: [*:0]const u8) anyerror!ZigLoadBSP {
    // read file
    const file = try std.fs.cwd().openFileZ(i_filename, .{});
    defer file.close();
    const fsize: usize = @intCast(try file.getEndPos());
    const bytes = try alloc.alloc(u8, fsize);
    defer alloc.free(bytes);
    _ = try file.readAll(bytes);

    // get bsp lumps
    const bspfile = bsp.Header.fromBytes(bytes);
    const edges = bspfile.getLumpArr(bspfile.edges, bsp.Edge);
    const faces = bspfile.getLumpArr(bspfile.faces, bsp.Face);
    const planes = bspfile.getLumpArr(bspfile.planes, bsp.Plane);
    const models = bspfile.getLumpArr(bspfile.models, bsp.Model);
    const texinfos = bspfile.getLumpArr(bspfile.texinfos, bsp.TexInfo);
    const textures = bspfile.getLumpPtr(bspfile.textures, bsp.TextureLump);
    const vertices = bspfile.getLumpArr(bspfile.vertices, bsp.VertexLump);
    const surfedges = bspfile.getLumpArr(bspfile.surfedges, bsp.SurfEdge);
    const clipnodes = bspfile.getLumpArr(bspfile.clipnodes, bsp.ClipNode);
    const miptexoff = textures.getOffsets();

    const TexFaceGrp = struct {
        iVertex0: u32,
        iVertexX: u32,
        nVertexs: u32,
        i3Index0: u32,
        i3IndexX: u32,
        n3Indexs: u32,
    };

    const texFaceGroup = try alloc.alloc(TexFaceGrp, miptexoff.len);
    defer alloc.free(texFaceGroup);
    @memset(std.mem.sliceAsBytes(texFaceGroup), 0);

    // count vertices and indices
    // const mdl = models[0];
    for (models) |mdl| {
        for (faces[mdl.iFace0..][0..mdl.nFaces]) |face| {
            const texinfo = texinfos[face.iTexInfo];
            const iMipTex = texinfo.iMipTex;
            texFaceGroup[iMipTex].nVertexs += face.nEdges;
            texFaceGroup[iMipTex].n3Indexs += face.nEdges - 2;
        }
    }

    // calculate continuous space in VBO and EBO
    var nVertexs: u32 = 0;
    var n3Indexs: u32 = 0;
    for (texFaceGroup) |*grp| {
        grp.iVertex0 = nVertexs;
        grp.iVertexX = nVertexs;
        nVertexs += grp.nVertexs;
        grp.i3Index0 = n3Indexs;
        grp.i3IndexX = n3Indexs;
        n3Indexs += grp.n3Indexs;
    }

    const Vertex = extern struct {
        pos: [3]f32,
        tex: [2]f32,
    };

    // fill VBO and EBO content
    const vbo = try alloc.alloc(Vertex, nVertexs);
    errdefer alloc.free(vbo);
    const ebo = try alloc.alloc([3]u32, n3Indexs);
    errdefer alloc.free(ebo);
    for (models) |mdl| {
        for (faces[mdl.iFace0..][0..mdl.nFaces]) |face| {
            const texinfo = texinfos[face.iTexInfo];
            const txgroup = &texFaceGroup[texinfo.iMipTex];
            const miptex = textures.getMipTex(miptexoff[texinfo.iMipTex]);
            const iVertexX = txgroup.iVertexX;
            const i3IndexX = txgroup.i3IndexX;
            txgroup.iVertexX += face.nEdges;
            txgroup.i3IndexX += face.nEdges - 2;
            for (surfedges[face.iEdge0..][0..face.nEdges], iVertexX..) |surfedge, i| {
                const abs = std.math.absCast(surfedge);
                const ivt = edges[abs][@intFromBool(surfedge < 0)];
                vbo[i] = .{
                    .pos = vertices[ivt],
                    .tex = texinfo.calcST(vertices[ivt], miptex.width, miptex.height),
                };
            }
            for (2..face.nEdges, 1.., i3IndexX..) |a, b, i|
                ebo[i] = .{
                    iVertexX + 0,
                    iVertexX + @as(u32, @intCast(a)),
                    iVertexX + @as(u32, @intCast(b)),
                };
        }
    }

    // load textures
    const ldtexs = try alloc.alloc(ZigBSPTex, miptexoff.len);
    errdefer {
        for (ldtexs) |l| if (l.pixels) |p| alloc.free(p[0 .. l.width * l.height]);
        alloc.free(ldtexs);
    }
    @memset(std.mem.sliceAsBytes(ldtexs), 0);
    for (ldtexs, miptexoff, 0..) |*ldtex, mipoff, iMipTex| {
        const miptex = textures.getMipTex(mipoff);
        ldtex.width = miptex.width;
        ldtex.height = miptex.height;
        const grp = texFaceGroup[iMipTex];
        ldtex.i3Index0 = grp.i3Index0;
        ldtex.n3Indexs = grp.n3Indexs;
        const colors = miptex.getColors();
        const indexs = miptex.getTexture(0).pixels;
        const pixels = try alloc.alloc([4]u8, indexs.len);
        errdefer alloc.free(pixels);
        for (indexs, pixels) |i, *p| {
            const c = colors[i];
            const a = if (c[0] < 10 and c[1] < 10 and c[2] > 240) @as(u8, 0) else 255;
            p.* = .{ c[0], c[1], c[2], a };
        }
        ldtex.pixels = pixels.ptr;
        const txname = miptex.getName();
        if (std.ascii.eqlIgnoreCase(txname, "aaatrigger") or
            std.ascii.eqlIgnoreCase(txname, "sky")) ldtex.skipped = 1;
        _ = std.c.printf("texture: %s\n", &miptex._name);
    }

    for (models) |m|
        std.debug.print("{}\n", .{m});

    // copy clipnodes
    const ldclips = try alloc.alloc(bsp.ClipNode, clipnodes.len);
    errdefer alloc.free(ldclips);
    @memcpy(ldclips, clipnodes);

    // copy planes
    const ldplane = try alloc.alloc(bsp.Plane, planes.len);
    errdefer alloc.free(ldplane);
    @memcpy(ldplane, planes);

    var maxdepth: u32 = 0;
    for (models) |mdl| {
        for (mdl.iHeadnodes[1..4]) |hn| {
            maxdepth = @max(maxdepth, recurseClipNode(ldclips.ptr, hn));
        }
    }
    _ = std.c.printf("clipnode maxdepth = %d\n", maxdepth);

    // return as bytes
    const vbo_data = std.mem.sliceAsBytes(vbo);
    const ebo_data = std.mem.sliceAsBytes(ebo);
    return .{
        .vbo_data = vbo_data.ptr,
        .vbo_size = vbo_data.len,
        .ebo_data = ebo_data.ptr,
        .ebo_size = ebo_data.len,
        .textures = ldtexs.ptr,
        .text_cnt = ldtexs.len,
        .clipnode = ldclips.ptr,
        .clip_cnt = ldclips.len,
        .world_h1 = models[0].iHeadnodes[1],
        .world_h2 = models[0].iHeadnodes[2],
        .world_h3 = models[0].iHeadnodes[3],
        .b_planes = ldplane.ptr,
        .planecnt = ldplane.len,
    };
}

fn recurseClipNode(nodes: [*]bsp.ClipNode, root: i32) u32 {
    var maxDepth: u32 = 0;
    if (root < 0) return 1;
    maxDepth = @max(maxDepth, recurseClipNode(nodes, nodes[@intCast(root)].iChildren[0]));
    maxDepth = @max(maxDepth, recurseClipNode(nodes, nodes[@intCast(root)].iChildren[1]));
    return maxDepth + 1;
}
