# Exporting a character from Silent Storm

Two commands turn a character's name into a rigged glTF 2.0 file with its
textures, its skeleton and its equipment:

```bash
A5_EXPORT_UNIT=Hanke A5_EXPORT_OUT=/tmp/hanke.txt silentstorm_mac <data root>

A5_BIN=<path to silentstorm_mac> python3 gltf_export.py \
    --complete <data root>/Complete --manifest /tmp/hanke.txt \
    --out Hanke.gltf --obj Hanke.obj
```

`A5_EXPORT_UNIT` takes a `RPGPers.UserName`, a decimal `RPGPers.ID`, or a
comma-separated list of either — a list writes `<out>.<name>.txt` per
character and costs one database load instead of one per character.

Nothing here is reverse-engineered. Every read is the engine's own loader,
running in the port, writing what it read back out as text.

## Why there is a manifest at all

Getting from a character's name to its meshes is five levels of database
pointer, and three of them are template indirections that look like row ids
and are not:

```
RPGPers 137 "Hanke"      ModelID 949, FaceID 1, UniformID 19, WeaponID 3
Models                   TemplateID 949 -> row 1298: geometry 1521, skeleton 8,
                                           Material0..3 = 843, 844, 845, 846
Materials                TemplateID 843 -> row 1265, TextureID 1878
Textures 1878            dxt1, 256x256
ComplexHeads 1 "Axis"    HeadID 6, Hair 0
Heads 6 "Axis01"         Material0 TemplateID 2018 -> Textures 4982
```

Resolve any of those `TemplateID`s as a row id and you get someone else's
texture — a concrete wall on a face. `A5_EXPORT_UNIT` walks it once with the
typed records and writes the answers out flat:

```
name  <UserName>
body  <geometry> <skeleton> <texture:alpha> x4
head  <head record> <texture:alpha>
gear  <bone> <geometry> <skeleton> <texture:alpha> x4
```

`:alpha` is `CMaterial::EAlpha`, which decides whether a texture's alpha is a
cutout, a blend or ignored. It matters: a rank patch the uniform does not wear
is a DXT1 texture that is entirely alpha 0, and without the cutout it renders
as a black rectangle on the shoulder.

## What a character is made of

**The body is four files, not one.** `CGameView::CreateSkin` draws it as
`N_MODEL_MATERIALS` parts and `GetID(SPartKey) = nID + (nPart << 16)`, so
geometry G lives in G, G+65536, G+131072, G+196608. The split is by material,
not by body part: part 1 of a soldier is one object holding the face *and*
both hands, because both are bare skin.

**The head is separate.** The body stops at the collar;
`CSetRender::AddMesh` hangs a head off bone 12 (`// CRAP - head bone`) and
`CGameView::CreateLSHead` places it with move `(0,-0.035,0)`, scale `0.014`
and rotate `(0,-FP_PI2,0)` — where `MakeMatrix` reads that middle angle as a
rotation about **Z**, not Y, and `CMSRConvert` pushes the scale inside, so the
order is scale, rotate, move, then the bone. The 0.014 is there because the
head mesh is in centimetres.

The head is LifeStudio Head middleware, but only the vertex positions are
inside the opaque animator stream; `CHeadMeshLoader::Recalc` reads the
topology, the UVs and the seam-copy table as ordinary engine chunks, and
`compat/src/lifestudio_stub.cpp` decodes the stream's neutral pose — which is
how the port gets static heads at all.

**Equipment hangs off named bones.** `GetItemsBindPlaces` attaches the
uniform's cap and backpack models, then whatever fixed model each of the nine
belt places carries, then the active weapon — in the hand on bone `Item`, or
slung on a bone named after the weapon type when it is `SUBTYPE_HEAVY`
(`Rifle`, `SubMachineGun`, `MachineGun`, `RocketLauncher`). The belt places
map onto `Slot1`..`Slot9` through `pszItemEffectors`, in an order that is not
the obvious one.

Equipment is **not** skinned: `CSetRender::AddMesh` draws each bound mesh with
`CreateMesh` under a `CAddBoneFilter`, so its vertices are already in the
bone's own space. In glTF that is a plain mesh node parented to the bone's
node, which also works for the sockets that carry no weights and so are not
joints of the skin.

## The skeleton

`Complete/Skeletons/<id>` holds `NAnimation::SBone` records — name, parent,
rest position, rotation, scale. `Complete/Binds/<geometry id>` holds the
inverse bind matrices, and `CBind::Recalc` pairs them positionally:

```c
matrices[i] = bones[i].world * invBindPoses[i];
```

so for a 63-bone skeleton with 46 binds, the joints are `bones[0:46]` and
bones 46-62 (`BackPack`, `Cap`, `Rifle`, `Slot1`..`Slot9`, ...) are the
equipment sockets. Blender imports those as Empties, since a glTF node is a
bone only if the skin lists it as a joint.

## Three conversions that are easy to get wrong

**Bind matrices.** `SHMatrix` is row-major (DirectX `_11.._44`) and glTF
accessors are column-major, so they are transposed on the way out. Getting it
wrong is silent: the model imports, the armature is there, and the mesh
renders at roughly double scale with the limbs splayed. The test that catches
it is `world(joint) * inverseBind`, which must equal the Z-up-to-Y-up root
rotation for every joint that carries weight.

**Texture coordinates.** The dumps write OBJ `vt`, which counts up from the
bottom; glTF `TEXCOORD_0` counts down from the top, so `gltf_export.py` flips
v on the way in and `write_obj` flips it back. On a leather coat an
upside-down texture is invisible; on a face it is not.

**Texture format.** The format code in an MMP header does not select the
compression — 5, 6 and 7 all turn up on textures game.db calls dxt1 — but the
file length does, because DXT1, 565 and 8888 mip chains differ by a factor of
four. `mmp2png.py` picks the codec by length and then checks the decoded
average colour against the one the database stores per texture.

## What is not exported

- **Animations.** `Animations.res`, the engine's own keyframe format. Only the
  bind pose is here.
- **Facial animation.** The LifeStudio muscle rig, `Sequences/` and
  `tree.mma`.
- **Inventory-driven gear.** The manifest carries the uniform's fixed models
  and the character's default weapon, not a live inventory.
