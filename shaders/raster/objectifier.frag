#version 450

// Only input is id
layout(location = 0) flat in uint id;

// Output is the id
layout(location = 0) out uint out_id;

void main()
{
	out_id = id + 1;
}
