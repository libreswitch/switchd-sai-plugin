OPS-SWITCHD-SAI-PLUGIN
============================

What is ops-switchd-sai-plugin?
--------------------------------------
ops-switchd-sai-plugin is the repo for the OpenSwitch Switch driver plugin
built on top of SAI.


What is the structure of the repository?
----------------------------------------
* `src` - contains all c source files.
* `include` - contains all c header files.

What is the license?
--------------------
Apache 2.0 license. For more details refer to [COPYING](http://git.openswitch.net/cgit/openswitch/ops-switchd-sai-plugin/tree/COPYING)

For general information about OpenSwitch project refer to http://www.openswitch.net.

How to run ops-switchd-sai-plugin stub?
---------------------------------
1. Clone SAI stub from OCP github repo (https://github.com/opencomputeproject/OCP-Networking-Project-Community-Contributions.git)
2. Compile and install SAI stub (https://github.com/opencomputeproject/OCP-Networking-Project-Community-Contributions/blob/master/sai/stub/README.md)
3. Compile ops-switchd-sai-plugin

Note: Not all functionality is supported by SAI stub at this point. Support of all required functionality will be added soon.
