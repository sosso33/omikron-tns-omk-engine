<div class="title">

# How Omikron Works

<p><strong>The engine of <em>Omikron: The Nomad Soul</em> (1999), and the replica that runs it again</strong></p>
<p>A book for developers, to be read in order</p>
<p><em>Draft of 2026-10-03</em></p>

</div>

<div class="pagebreak"></div>

<!-- TOC -->

# Before you start

## Who this is for

You write games or tools. Maybe you know Unity well, you have read some
assembly, written some C and C++, and pushed triangles through OpenGL. You
have never looked inside a 1999 engine, and you would like to understand one
properly: not the list of its file formats, but *why it is built the way it
is*, and what it takes to rebuild it on hardware its authors never saw.

That is what this book tries to give you. It follows one game, *Omikron: The
Nomad Soul*, from the moment its executable starts to the moment a pixel
reaches the screen, and then follows the project that reverse-engineered it
(**OMK**) through the rebuilding.

## How it differs from the manual

This repository already has a **manual** (`manual/`): thirteen chapters, each
with a plain summary and a technical account, the addresses of the functions,
the offsets of the fields, the checks that assert every number. It is a
reference. You go to it with a question.

This book is the other thing. It is meant to be read **from the first page to
the last**, and each chapter leans on the ones before it. It uses fewer
numbers, and only the ones that carry an idea. It explains mechanisms with
pictures and analogies, and when it needs a precise fact it tells you which
chapter of the manual, or which document under `docs/`, holds it. If the two
ever disagree, the documents under `docs/` win: this book, like the manual, is
*derivative*, a retelling of findings recorded elsewhere with their evidence.

## How it is organised

**Part I** looks at the game as a machine: what the executable is, what one
frame does, and where the game really lives, which is in its data files.

**Part II** is about behaviour: the tiny computer inside the game that runs
its scripts, the places the world is made of, the bodies that walk around in
them, and the stories told with conversations and cutscenes.

**Part III** is about output: how a frame was drawn in 1999, how sound works
without a mixer, and how the interface is built.

**Part IV** is short and a little different: how you read an engine you do
not have the source code of, and how you know when you have read it right.

**Part V** is the rebuilding: the shape of the replica, the renderers behind
it, the long work of making it fast enough for a handheld, and how that work
compares with what the original engine did.

Throughout, boxes like this one translate an idea into terms you may already
know:

> **From Unity:** a box like this maps an Omikron mechanism onto the closest
> Unity or OpenGL concept. The mapping is never exact, and the box says where
> it breaks.

## A running example

One scene comes back again and again: **Kay'l standing in Anekbah's main
street**, the city's crowd walking past, a street lamp glowing, a shop sign
hanging on a wall. It is the port's standard test scene, and nearly every
subsystem in this book shows up in it somewhere. When a chapter introduces a
mechanism, it will usually tell you where that mechanism is in this street.

