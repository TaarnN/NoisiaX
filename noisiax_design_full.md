# Architecting a Deterministic Simulation Engine: A Hybrid Push-Pull Framework for Reproducible AI Research and Probabilistic Forecasting

## Executive Summary and Core Architectural Principles

The objective of this research is to architect a high-performance, deterministic C++ simulation library designed to model complex interdependent systems. This library serves as a foundational tool for scientific-grade research into artificial intelligence decision-making under systemic stress and for probabilistic forecasting of real-world events, accounting for full-system context <user>. The core concept revolves around simulating a "butterfly effect," where changes propagate through a network of variables governed by a mathematically defined seed system. The architecture is guided by four non-negotiable principles: strict determinism by default, a hybrid static-dynamic dependency graph, a hybrid push-pull propagation strategy, and a design that scales seamlessly from simple scenarios to large-scale reality simulations without architectural compromise <user>. These principles form the bedrock upon which every subsequent design decision is based, ensuring the final product is scientifically rigorous, performant, flexible, and scalable.

The most critical principle is **strict determinism**. The library must guarantee that given an identical initial state, seed, and sequence of events, it will produce bitwise-identical outputs every time <user>. This is paramount for scientific reproducibility, enabling researchers to debug models, validate results, and compare different versions of their simulations with confidence [[51](https://pmc.ncbi.nlm.nih.gov/articles/PMC5016202/)]. To achieve this, all internal computations, including those involving floating-point arithmetic, must be strictly controlled. Stochastic behavior, such as probabilistic transitions or Monte Carlo runs, is not to be baked into the core propagation logic but rather treated as an explicit, optional layer built upon the deterministic engine <user>. This ensures the underlying system remains scientifically trustworthy while still allowing for the modeling of real-world uncertainty. This aligns with established practices in scientific computing, where deterministic execution is the norm for verification and validation, and stochasticity is applied as a separate analysis technique [[28](https://arxiv.org/html/2604.03606v1), [46](https://www.sciencedirect.com/science/article/am/pii/S157401372400039X)].

The second principle is the adoption of a **hybrid dependency graph model**. While a fully dynamic graph offers maximum flexibility, it introduces significant runtime overhead and complexity, hindering performance optimizations and making the system difficult to analyze <user>[[11](https://stackoverflow.com/questions/6603456/c-c-library-for-dynamic-graphs)]. Therefore, the architecture is predicated on a static dependency graph that is defined at simulation setup. This static backbone allows for compile-time and pre-runtime optimizations, simplifies validation, and enables more predictable propagation paths [[13](https://theses.hal.science/tel-05144827v1/file/143807_JAIME_2025_archivage.pdf)]. However, to accommodate real-world phenomena where relationships can evolve, this static foundation is augmented with a dynamic overlay graph. This overlay supports relationships that are added, removed, or modified during runtime based on rules, thresholds, or learned behaviors <user>[[218](https://dl.acm.org/doi/full/10.1145/3731599.3767555), [219](https://link.springer.com/content/pdf/10.1007/978-3-662-03035-6.pdf)]. This hybrid approach provides the best of both worlds: the performance and predictability of a static structure with the necessary flexibility for evolving systems.

The third principle dictates the use of a **hybrid push-pull propagation engine**. For large-scale simulations, neither a pure push-based model nor a pure pull-based model is optimal <user>. A naive push model, where a change immediately triggers re-computation in all dependents, can lead to unnecessary work and performance degradation, especially in dense graphs [[5](https://stable-baselines3.readthedocs.io/_/downloads/en/master/pdf/)]. Conversely, a pure pull model, where values are only computed when requested, can become inefficient if many dependent values are accessed repeatedly or if a consistent global update is required [[24](https://onlinelibrary.wiley.com/doi/10.1155/2023/6601690)]. The proposed solution is a hybrid strategy: **push for invalidation, pull for recomputation**. When a variable's value changes, the engine immediately "pushes" an invalidation signal to its direct dependents, marking them as stale. It does not recompute their values right away. Recomputation is deferred until a value is explicitly "pulled" or requested by the user. At that moment, the engine performs a lazy, recursive calculation of the requested value and all its upstream dependencies, caches the result, and returns it <user>[[34](http://tnm.engin.umich.edu/wp-content/uploads/sites/353/2017/12/CASES_2012_Lazy.pdf), [35](https://webdocs.cs.ualberta.ca/~holte/T26/caching-lazy-eval.html)]. This approach minimizes wasted computational effort, maintains cache coherence, and provides a predictable framework for parallelization, combining the efficiency of invalidation with the economy of lazy evaluation.

Finally, the architecture must be inherently **scalable**. It must be capable of supporting simulations ranging from simple, isolated scenarios to extremely large, complex models of reality suitable for practical forecasting <user>. This requires careful consideration of data structures, memory management, and parallelization strategies from the outset. The design must avoid bottlenecks that become apparent only at scale. This involves choosing data layouts optimized for modern hardware, implementing incremental computation to limit the scope of updates, and designing a parallelization model that can effectively harness multi-core CPUs and GPUs [[62](https://www.mdpi.com/2076-3417/15/17/9706), [73](https://www.academia.edu/2718984/Superflo_Making_Haskell_faster)]. The library's modularity and component-based design, inspired by frameworks like Trilinos and MOOSE, will be crucial for managing complexity and allowing the system to grow without architectural decay [[95](https://arxiv.org/html/2503.08126v2), [211](https://www.researchgate.net/publication/222823564_MOOSE_A_parallel_computational_framework_for_coupled_systems_of_nonlinear_equations)].

In addressing the user's initial proposal of embedding a "seed in each parameter," this report argues for a significant redesign. While the concept of a cascading influence is central, embedding the logic within the parameters themselves creates tight coupling, makes dependencies implicit and hard to manage, and violates separation of concerns [[167](https://stackoverflow.com/questions/54602540/design-pattern-for-dependency-graph)]. The recommended alternative is an explicit, centralized propagation engine. This engine acts as an orchestrator, operating on a formal dependency graph where edges contain the deterministic mathematical functions that define how influence (the "seed") propagates from one variable to another. This redesign transforms the system from a fragile, monolithic construct into a robust, decoupled, and highly flexible architecture that better serves the goals of scientific research and high-performance simulation.

## Core Data Model and Dependency Graph Abstraction

The foundation of the simulation library rests on a well-defined core data model and a sophisticated abstraction for representing interdependencies. These components dictate how state is stored, how relationships are defined, and how information flows through the system. The design prioritizes performance, clarity, and the ability to support both static and dynamic relationships.

The primary container for the simulation's state is the `SimulationState` class. This object holds all the variables (or parameters) that constitute the system being modeled. To maximize performance, particularly for large-scale simulations amenable to vectorization and GPU acceleration, the `SimulationState` should employ a Structure-of-Arrays (SoA) memory layout [[102](https://www.sciencedirect.com/science/article/pii/S0898122120300146)]. In an SoA layout, instead of storing data as an array of structs (AoS), where each struct contains all fields for a single entity (e.g., `{x1, y1, z1}, {x2, y2, z2}, ...`), the data is stored as an array of structures (AoS), where each array holds all the values for a single field (e.g., `{x1, x2, ...}`, `{y1, y2, ...}`) [[103](https://theses.hal.science/tel-03118420v2/file/Cassagne2020%20-%20Optimization%20and%20Parallelization%20Methods%20for%20the%20Software-Defined%20Radio.pdf), [239](https://inria.hal.science/tel-03105625/file/hdr_oaumage.pdf)]. This arrangement ensures that data corresponding to the same attribute is stored contiguously in memory, which is highly beneficial for CPU cache performance and for leveraging Single Instruction, Multiple Data (SIMD) instructions available on modern processors [[59](https://arxiv.org/html/2410.15625v3), [60](https://openreview.net/pdf?id=RqRQfFQrxi)]. It also simplifies memory access patterns for parallel processing frameworks like CUDA, where coalesced memory access from threads in a warp is critical for performance [[101](https://www.linkedin.com/posts/keerthiningegowda_in-a-parallel-universe-memory-is-scarce-activity-7430619674733600768-UkSi)]. The choice of layout can be made explicit to developers using a C++ language extension based on attributes, allowing them to guide the compiler in selecting optimal memory arrangements [[61](https://www.researchgate.net/publication/394278426_Annotation-Guided_AoS-to-SoA_Conversions_and_GPU_Offloading_With_Data_Views_in_C), [185](https://www.researchgate.net/publication/381308249_An_extension_of_C_with_memory-centric_specifications_for_HPC_to_reduce_memory_footprints_and_streamline_MPI_development)].

To reference individual parameters within the `SimulationState`, the library uses a lightweight, opaque identifier called a `ParameterHandle`. This handle is not a raw pointer but rather a simple type, such as a `struct` containing an index or a unique ID, wrapped in a class to prevent misuse [[64](https://stackoverflow.com/questions/1809670/how-to-implement-serialization-to-a-stream-for-a-class)]. Using handles instead of pointers provides several advantages. It decouples the dependency graph from direct pointers to state objects, preventing dangling pointers if the state object were to be moved in memory. More importantly, handles are trivially serializable, which is essential for saving simulation checkpoints and enabling deterministic replay [[64](https://stackoverflow.com/questions/1809670/how-to-implement-serialization-to-a-stream-for-a-class)]. They act as stable, long-lived identifiers for a parameter throughout its lifecycle.

The heart of the system's relational model is the `DependencyGraph`. This is a directed graph where nodes represent `ParameterHandle`s and edges represent the causal relationships between them [[66](https://hal.science/tel-03515795v1/file/thesis-apietri-2022-01-06.pdf)]. The design of this graph is a hybrid, reflecting one of the core architectural principles. It consists of two distinct components:

1.  **Static Backbone Graph:** This is the core, foundational graph defined at simulation setup. It represents the immutable, long-term relationships in the system. Its structure is validated at compile-time or pre-runtime, allowing for extensive optimization. For instance, if a portion of the graph is a Directed Acyclic Graph (DAG), a topological sort can be performed once to establish an optimal evaluation order [[151](https://www.arxiv.org/list/cs/new?skip=200&show=2000)]. This graph forms the predictable, high-performance skeleton of the simulation.
2.  **Dynamic Overlay Graph:** This component exists to allow relationships to evolve during simulation execution. Rules, triggers, or even machine learning models can add, remove, or modify edges in this overlay graph in response to changing system states or external events [[219](https://link.springer.com/content/pdf/10.1007/978-3-662-03035-6.pdf)]. This provides the necessary flexibility to model emergent behaviors and adaptive systems without compromising the integrity of the static backbone. The interaction between these two graphs is managed by the propagation engine, which queries both during a propagation cycle.

The implementation of the `DependencyGraph` itself should favor efficiency for sparse graphs, which are common in many real-world systems. An adjacency list representation is typically superior to an adjacency matrix for this purpose, as it uses less memory and traverses faster when the number of edges is much smaller than the square of the number of nodes [[15](https://dev.to/zigrazor/cxxgraph-vs-boost-graph-library-the-complete-2025-comparison-guide-4n4)]. Each node in the adjacency list would store a collection of outgoing edges. Each edge stores metadata relevant to the propagation process, including:
*   **Target Parameter Handle:** The destination node in the graph.
*   **Propagation Function:** A callable object (e.g., a function pointer, lambda, or functor) that embodies the "fixed mathematical formula" for how influence is passed. This function takes the source parameter's new value and any other necessary context to compute the contribution to the target parameter.
*   **Metadata:** Additional data such as a weight or coefficient for the dependency, and potentially versioning information for cache invalidation purposes [[34](http://tnm.engin.umich.edu/wp-content/uploads/sites/353/2017/12/CASES_2012_Lazy.pdf)].

This explicit, graph-based approach to dependencies stands in stark contrast to the initially proposed "seed embedded in each parameter." That model would have hidden the graph's structure inside the parameter objects, making it difficult to inspect, validate, or reason about the system's overall connectivity [[138](https://hal.science/tel-05213738v1/file/ASE-JURY.pdf), [167](https://stackoverflow.com/questions/54602540/design-pattern-for-dependency-graph)]. The proposed design makes the dependency graph a first-class citizen, enabling introspection, visualization, and formal analysis of the system's structure—a critical requirement for scientific research [[143](https://www.researchgate.net/publication/221302634_Visualization_of_Program_Dependence_Graphs)]. This clear separation of data (`SimulationState`) from the relational logic (`DependencyGraph`) is a cornerstone of a modular, maintainable, and extensible architecture.

## The Hybrid Push-Pull Propagation Engine

The propagation engine is the computational core of the simulation library, responsible for driving state changes and ensuring consistency across the entire system of interdependent variables. It implements the hybrid push-pull strategy, a sophisticated mechanism designed to balance immediate invalidation with deferred, efficient recomputation. This engine orchestrates the flow of information from changed variables outwards to their dependents, respecting the principles of determinism and performance.

The engine's operation is divided into two distinct phases: a push phase for invalidation and a pull phase for lazy recomputation. This duality is central to its efficiency and effectiveness in large-scale simulations <user>.

**Phase 1: Push-Based Invalidation**

When a user modifies the value of a parameter through the engine's public API, the first action is not to recompute anything. Instead, the engine initiates the push phase. It takes the `ParameterHandle` of the newly changed parameter and uses it to query the `DependencyGraph` for all nodes connected by an outgoing edge—these are the direct dependents of the modified parameter. For each dependent, the engine immediately marks its corresponding value as "dirty" or "stale". This marking signifies that the cached value is no longer valid and needs to be recalculated before it can be used again. This invalidation can be tracked efficiently using a simple boolean flag associated with each parameter in the `SimulationState`, or more robustly with a versioning scheme where each state update increments a global timestamp or version number, and each parameter stores the version number of the state it was last computed from [[34](http://tnm.engin.umich.edu/wp-content/uploads/sites/353/2017/12/CASES_2012_Lazy.pdf), [92](https://dl.acm.org/doi/full/10.1145/3689769)].

This push-based invalidation is computationally inexpensive and happens synchronously with the user's update. Its primary benefit is that it guarantees immediate propagation of "freshness" information through the dependency chain. No stale data can persist in the system after a state change has been committed. This contrasts sharply with a purely pull-based system where a user might read a stale value before the system had a chance to invalidate it. By pushing the invalidation signal, the engine establishes a consistent starting point for the next phase. The cost of this push is proportional to the out-degree of the modified node, which is typically low in sparse graphs, making it a very efficient operation.

**Phase 2: Pull-Based Lazy Recomputation**

After a parameter is marked dirty, its value is not immediately recalculated. The engine defers this potentially expensive computation until the value is explicitly needed. This is the pull phase. A pull operation occurs when a user requests the value of a parameter. Before returning the value, the engine checks its status. If the parameter is clean (i.e., up-to-date), it simply returns the cached value. However, if the parameter is dirty, the engine must initiate a lazy recomputation. This process is recursive and works its way upstream along the dependency graph.

The algorithm for a pull request on a dirty parameter `P` is as follows:
1.  The engine inspects `P`'s incoming edges in the `DependencyGraph` to identify all of its prerequisite parameters (i.e., the sources of its dependencies).
2.  For each prerequisite parameter, it recursively invokes the same pull operation. If a prerequisite is dirty, this triggers its own recursive computation, and so on.
3.  This recursive descent continues until it reaches a set of parameters that are all clean. These are the foundational inputs for the computation of `P`.
4.  Once all prerequisites are resolved and their current, valid values are obtained, the engine iterates over the incoming edges of `P`. For each dependency edge, it invokes the associated propagation function, passing the newly pulled value from the source parameter.
5.  The results from all propagation functions are combined (e.g., summed) according to the edge weights to calculate the new value for `P`.
6.  This newly computed value is then cached in `P`'s location within the `SimulationState`.
7.  The engine sets `P`'s status to "clean" (or updates its version number).
8.  Finally, the engine returns the newly computed value to the caller.

This pull-based recomputation is highly efficient because it only computes what is necessary. If a part of the dependency graph is never accessed, its dirty state may persist indefinitely without any computational penalty. This avoids the "fanning-out" problem of a naive eager push model, where a single change could trigger a massive, wasteful cascade of recomputations across the entire graph [[25](https://dl.acm.org/doi/pdf/10.1145/1596638.1596643)]. The combination of push invalidation and pull recomputation provides a powerful trade-off: it ensures data consistency with minimal immediate cost, yet avoids unnecessary work by adopting a lazy evaluation strategy [[35](https://webdocs.cs.ualberta.ca/~holte/T26/caching-lazy-eval.html), [177](https://stackoverflow.com/questions/13575498/lazy-evaluation-and-reactive-programming)]. This makes it ideal for large, sparse systems where changes are often localized. The engine can even provide an optional "eager propagation" mode for specific, performance-critical regions where the cost of immediate recomputation is lower than the overhead of managing lazy dependencies [[37](https://developer.nvidia.com/docs/drive/drive-os/6.0.7/public/drive-os-tensorrt/pdf/NVIDIA-TensorRT-8.6.10-Developer-Guide-for-DRIVE-OS.pdf)].

## Performance Architecture: Optimizing for Scale and Speed

Achieving high performance is a primary goal of the library's design. The performance architecture is not an afterthought but is woven into the core abstractions and engine mechanics from the beginning. It employs a multi-faceted strategy encompassing memory layout optimization, incremental computation, and advanced parallelization techniques to ensure the library can scale effectively.

A cornerstone of the performance strategy is **memory layout optimization**. As detailed in the core data model section, the `SimulationState` utilizes a Structure-of-Arrays (SoA) layout. This is a deliberate choice to maximize data locality and enable vectorization. When parameters are accessed sequentially, their data resides in contiguous memory blocks, leading to efficient CPU cache utilization and minimizing cache misses [[100](https://www.cs.wustl.edu/~roger/566S.s21/EijkhoutIntroToHPC.pdf)]. Furthermore, this layout is exceptionally well-suited for SIMD instruction sets, which can perform the same operation on multiple data points simultaneously. For example, applying a propagation function to an entire array of particles can be done in a single vectorized instruction, dramatically accelerating computation [[59](https://arxiv.org/html/2410.15625v3)]. This approach is mirrored in high-performance computing libraries and GPU programming guides, which consistently advocate for SoA layouts over AoS for data-parallel tasks [[62](https://www.mdpi.com/2076-3417/15/17/9706), [101](https://www.linkedin.com/posts/keerthiningegowda_in-a-parallel-universe-memory-is-scarce-activity-7430619674733600768-UkSi), [239](https://inria.hal.science/tel-03105625/file/hdr_oaumage.pdf)]. The design also extends to the dependency graph's adjacency lists, which should be implemented with contiguous storage (e.g., `std::vector`) to ensure cache-friendly traversal.

The library's design inherently supports **incremental computation**, a key feature enabled by the lazy pull model of the propagation engine. When a state change occurs, only the affected subgraph needs to be recomputed. The cost of an update is directly proportional to the size of the region impacted by the change, not the total size of the simulation [[54](https://arxiv.org/pdf/2210.01076)]. This is a profound advantage over batch processing approaches where the entire system state must be recomputed at each time step. Batching is also supported at a higher level; the engine can accept a batch of parameters to be invalidated, and the propagation can be run once over the entire batch, reducing the overhead of repeatedly entering and exiting the propagation loop. This makes the system highly efficient for simulations where changes are sparse and localized.

For massive scaling, the architecture incorporates a **multi-layered parallelization strategy** targeting both data and task parallelism.

1.  **Data Parallelism:** Computations that are independent and operate on large datasets are prime candidates for data parallelism. The SoA layout of `SimulationState` facilitates this. Operations like applying a force to all particles in a simulation or updating the state of all cells in a grid can be partitioned and executed concurrently on different CPU cores using frameworks like Intel Threading Building Blocks (oneTBB) or OpenMP [[94](https://cdrdv2-public.intel.com/835537/onetbb_developer-guide-api-reference_2022.0-772616-835537.pdf), [160](https://www.researchgate.net/profile/Luisa-Damore/publication/323938867_Performance_Assessment_of_the_Incremental_Strong_Constraints_4DVAR_Algorithm_in_ROMS/links/5c4188b5a6fdccd6b5b59d2d/Performance-Assessment-of-the-Incremental-Strong-Constraints-4DVAR-Algorithm-in-ROMS.pdf)]. For even greater speedup, these data-parallel kernels can be offloaded to GPUs using CUDA or similar APIs, leveraging massively parallel architectures [[65](https://docs.nvidia.com/cuda/cuda-programming-guide/pdf/cuda-programming-guide.pdf), [93](https://docs.nvidia.com/cuda/pdf/CUDA_C_Programming_Guide.pdf)]. The propagation functions themselves can also be designed to be data-parallel, operating on arrays of input values.

2.  **Task Parallelism:** The dependency graph itself provides opportunities for task parallelism. Disconnected components or independent subtrees within the graph can be propagated in parallel. OneTBB's flow graph is an excellent conceptual model for this, where nodes represent computational tasks (e.g., propagating a subgraph) and edges define dependencies between them [[124](https://cdrdv2-public.intel.com/816905/onetbb_developer-guide-api-reference_2021.12-772616-816905.pdf)]. The propagation engine can partition the graph and schedule these propagation tasks onto a thread pool, allowing multiple independent parts of the simulation to progress concurrently. Synchronization between these tasks must be handled carefully to maintain determinism, for instance by using a transactional model or by ensuring that tasks do not interfere with each other's data [[128](https://www.researchgate.net/publication/221474601_STAMP_Stanford_Transactional_Applications_for_Multi-Processing)].

3.  **Deterministic Parallel Random Number Generation:** A major challenge in parallel stochastic simulations is ensuring determinism. Simply spawning multiple threads with default random number generators (RNGs) will lead to non-reproducible results due to race conditions and the complex interactions of the RNG algorithms [[42](https://arxiv.org/pdf/2402.07530)]. The library must implement a system for deterministic parallel RNG. A common and effective approach is to assign a unique, non-overlapping stream of random numbers to each thread or task [[28](https://arxiv.org/html/2604.03606v1)]. The main simulation thread can generate a master seed, which is then used to derive seeds for each worker stream using a counter-based method [[1](https://stackoverflow.com/questions/16613568/using-pseudo-random-number-engines-in-deterministic-multi-threaded-applications), [47](https://stackoverflow.com/questions/72385805/forking-a-random-number-generator-deterministically)]. This ensures that regardless of how the workload is scheduled across cores, the sequence of random numbers encountered by each thread is always the same, thus preserving determinism [[44](https://brian2.readthedocs.io/en/2.9.0/introduction/release_notes.html)].

This combination of optimized memory layouts, incremental lazy evaluation, and a robust, deterministic parallelization model forms a comprehensive performance architecture. It is designed to extract maximum throughput from modern heterogeneous hardware—from multi-core CPUs to GPUs—while maintaining the strict consistency required for scientific simulation.

## Serialization, Persistence, and Deterministic Replay

For a simulation library intended for scientific research, the ability to save, load, and, most critically, deterministically replay experiments is not a secondary feature but a fundamental requirement. The design must incorporate a robust serialization and persistence subsystem from the outset to guarantee that results are reproducible, a cornerstone of the scientific method [[51](https://pmc.ncbi.nlm.nih.gov/articles/PMC5016202/), [81](http://www.arxivdaily.com/thread/71868)]. This subsystem is intrinsically linked to the library's commitment to strict determinism.

The serialization strategy must capture everything necessary to completely reconstruct a simulation's state at a given point in time. This involves serializing three primary components:

1.  **The Simulation State:** The complete binary dump of the `SimulationState` object is the most critical piece. Given its SoA layout, this is typically a large, contiguous block of memory representing the values of all parameters. Serializing this block preserves the exact numerical state of the system.

2.  **The Dependency Graph:** Both the static backbone graph and the current configuration of the dynamic overlay graph must be serialized. The static graph, being fixed, can be serialized once and stored separately as a definition of the simulation's structure. The dynamic overlay, however, represents a mutable state and must be saved as part of the simulation checkpoint. The serialization format for the graph should capture all nodes, edges, and the propagation functions associated with each edge. Storing the functions themselves can be challenging, but a viable approach is to serialize a unique identifier or name for the function, coupled with its source code hash or a version string, allowing the system to look up and reconstruct the correct logic upon deserialization [[64](https://stackoverflow.com/questions/1809670/how-to-implement-serialization-to-a-stream-for-a-class)].

3.  **System Metadata:** This includes the current simulation timestamp or version number, which tracks the logical progression of the simulation. Most importantly, it includes the seed used for the deterministic random number generator (RNG). Capturing this single integer is sufficient to reproduce the entire stochastic history of the simulation, assuming the RNG algorithm is fixed [[21](https://www.researchgate.net/publication/359773252_Spike_-_a_tool_for_reproducible_simulation_experiments), [117](https://stackoverflow.com/questions/65851238/storing-random-device-seeds-for-monte-carlo-simulations)].

The process for achieving **deterministic replay** is straightforward once a checkpoint has been saved. To reproduce a previous experiment, the user would:
1.  Deserialize the saved checkpoint file. This reconstructs a pristine instance of the `SimulationState`, rebuilds the `DependencyGraph` (both static and dynamic components), and initializes the RNG with the exact seed recorded during the original run.
2.  Re-execute the same sequence of user actions or event-driven updates that were performed in the original simulation. Because the initial state is identical, the dependency graph is identical, the RNG produces the identical sequence of pseudo-random numbers, and the core propagation engine is deterministic by design, the resulting state transitions will be bitwise identical to the original run [[255](https://arxiv.org/html/2604.08369v1)].

This capability is invaluable for several reasons. It allows researchers to debug unexpected behavior by running the exact same scenario that produced the bug. It enables the validation of model revisions by comparing outputs before and after a change. It is also essential for conducting Monte Carlo studies, where many simulations are run with slightly varied parameters or seeds; having a baseline deterministic run provides a crucial reference point for analysis [[30](https://pubs.acs.org/doi/10.1021/acs.iecr.0c05795), [46](https://www.sciencedirect.com/science/article/am/pii/S157401372400039X)].

The serialization mechanism itself should be designed for performance. Binary serialization formats are preferred over text-based ones like JSON or XML for simulation data, as they are significantly faster to write and read and produce much smaller files. Libraries like Boost.Serialization can provide a solid foundation for implementing this, handling much of the boilerplate code for mapping C++ objects to and from byte streams [[64](https://stackoverflow.com/questions/1809670/how-to-implement-serialization-to-a-stream-for-a-class)]. The API should provide simple functions like `save_checkpoint(const std::string& filename)` and `load_checkpoint(const std::string& filename)`. Internally, the engine must manage the serialization of complex types, particularly the propagation functions. This might involve a factory pattern where functions are registered by name, allowing the deserializer to instantiate the correct function object based on the identifier stored in the file. This ensures that the system can be fully reconstructed, regardless of how complex the simulation's logic becomes.

## Testing, Validation, Risks, and Extensibility

A robust quality assurance framework is essential for a library intended for scientific research. This framework must encompass a multi-layered testing strategy, a clear understanding of architectural risks and limitations, and a design that promotes long-term extensibility. The ultimate goal is to build a system that is not only fast and scalable but also reliable, verifiable, and adaptable to future research needs.

The **testing and validation strategy** should be comprehensive and address different aspects of the library's functionality.

*   **Unit Testing:** Individual components must be rigorously tested in isolation. This includes testing the `SimulationState` for correct memory layout and access. Crucially, each propagation function (the mathematical formulas defining how influence flows) must have its own unit tests. These tests should verify that the function produces the expected output for a wide range of inputs, covering normal cases, edge cases, and boundary conditions. Fuzz testing can also be employed to probe for unexpected crashes or violations of assumptions in these functions [[85](https://arxivdaily.com/thread/58503)].
*   **Integration and Regression Testing:** Small, predefined scenarios with known outcomes should be created to test the integration of components. These tests exercise the end-to-end pipeline: setting an initial state, performing a series of updates, propagating changes, and verifying the final state against a pre-calculated golden standard. A key part of this suite is the **reproducibility test**: running the same scenario twice and asserting that the final state is bitwise identical. This is the ultimate check for the library's deterministic nature. Every new feature or bug fix must pass this regression suite.
*   **Benchmarking and Performance Validation:** The library must be subjected to systematic performance benchmarking. This involves measuring execution time and resource consumption for simulations of varying sizes and complexities. Benchmarks should be run on representative hardware to establish performance baselines and detect regressions. Key metrics include propagation latency per time step, memory usage, and scaling efficiency on multi-core systems. Comparisons against established simulation frameworks like NEST, libRoadRunner, or OMNeT++ can provide valuable context [[50](https://dl.acm.org/doi/10.1145/3653975), [91](https://pmc.ncbi.nlm.nih.gov/articles/PMC9825722/), [175](https://www.frontiersin.org/journals/neuroinformatics/articles/10.3389/fninf.2022.837549/full)]. The Benchopt framework offers insights into creating standardized benchmarks for scientific software written in various languages, including C/C++ [[181](https://hal.science/hal-03830604/document)].

Despite its strengths, the proposed architecture carries certain **risks and limitations** that must be acknowledged and mitigated.

| Risk / Limitation | Description | Mitigation Strategy |
| :--- | :--- | :--- |
| **Complexity of Hybrid Graph Management** | Managing the interaction between the static graph and the dynamic overlay at runtime could introduce subtle bugs, such as creating cycles in the dependency graph or modifying the graph structure during a propagation cycle, leading to infinite loops or inconsistent states. | Implement a "modification guard" that prevents any graph modifications during active propagation. Modifications should only be allowed in designated "setup" or "configuration" phases. The dynamic overlay should be subject to validation rules to prevent the creation of invalid structures. |
| **Performance Degradation from Cache Thrashing** | In a very large and dense dependency graph, the recursive dependency traversal during a pull operation could cause significant cache misses ("thrashing"), negating the benefits of data locality and leading to unpredictable performance. | Employ a "dependency prefetching" strategy, where the engine attempts to load nodes of the target subtree into the cache before initiating a pull. Provide an API for "eager propagation" on hotspots, allowing users to override the lazy pull behavior for critical paths where performance is more important than minimizing recomputation. |
| **Limited Scope of the Propagation Model** | The library operates on a predefined dependency graph. It is a tool for testing hypotheses about a system's structure, not for discovering that structure from scratch. Real-world systems have emergent properties and unknown dependencies that cannot be captured in a fixed graph. | Position the library as a platform for hypothesis testing and controlled experimentation. It complements, rather than replaces, machine learning approaches for discovering causal links from data [[222](https://arxiv.org/abs/2510.24639), [252](https://dl.acm.org/doi/10.1145/3637225)]. The dynamic overlay is a mechanism to explore how the system's structure itself might evolve. |

Finally, the architecture must be designed for **extensibility**. The goal is to create a library that can serve as a foundation for long-term research, adapting to new modeling paradigms and scientific questions. This is achieved through a modular, plugin-based design philosophy, inspired by frameworks like MOOSE and Trilinos [[95](https://arxiv.org/html/2503.08126v2), [211](https://www.researchgate.net/publication/222823564_MOOSE_A_parallel_computational_framework_for_coupled_systems_of_nonlinear_equations)].
*   **Plugin Architecture:** The core engine should be kept minimal and focused on state management and propagation. New types of parameters, new categories of propagation functions, and even entirely new propagation strategies (e.g., switching to a different engine for a specific sub-simulation) should be implemented as plugins that integrate with the core framework. This allows for innovation without requiring changes to the core library, promoting stability and reuse.
*   **Well-Defined Interfaces:** All interactions between the core engine and plugins should be mediated through clean, stable Application Programming Interfaces (APIs). For example, there should be a base class for propagation functions that defines a standard interface (e.g., `virtual double operator()(const std::vector<double>& inputs) = 0;`). This allows users to easily define their own custom functions by inheriting from this base class.
*   **Configuration via Scripting:** To make the library accessible to non-C++ experts (e.g., domain scientists), a high-level scripting interface (e.g., Python) should be provided. This interface would expose the core library's functionality, allowing users to define complex simulations, parameters, and dependencies through scripts rather than C++ code. This approach is used by successful scientific libraries like NEURON and MOOSEnger to broaden their user base [[134](https://www.researchgate.net/publication/23980966_PyNN_A_Common_Interface_for_Neuronal_Network_Simulators), [188](https://arxiv.org/pdf/2603.04756)].

By combining a rigorous testing regimen with proactive risk mitigation and a forward-looking design for extensibility, the resulting library will not only meet the immediate research goals but also stand as a durable and adaptable tool for future explorations into complex systems.

# Scenario Authoring System: Architectural Specification

## I. Scenario Creation Workflow (End-to-End Pipeline)

The authoring workflow is strictly linear and gate-driven. Each stage produces an immutable artifact that feeds into the next, ensuring that dependencies are resolved before validation, and constraints are verified before events are scheduled.

**Pipeline Sequence:**
`Goal → Assumptions → Entities → Variables → Dependencies → Constraints → Events → Evaluation Criteria`

**Why This Ordering Is Mandatory:**

1. **Goal Definition** establishes the simulation's purpose and success metrics. Without it, parameter selection becomes arbitrary.
2. **Assumptions** bound the simulation's scope (e.g., "ignore atmospheric drag", "assume rational actors"). They act as validation filters for subsequent steps.
3. **Entities** define the structural containers (agents, regions, systems). Variables cannot exist without an entity namespace to prevent naming collisions.
4. **Variables** declare state space, types, units, and initial values. Dependencies require fully declared variables to resolve handles.
5. **Dependencies** encode the propagation topology. They must reference existing variables and respect type/unit compatibility.
6. **Constraints** impose bounds on valid states (e.g., `mass >= 0`, `budget_A + budget_B = total`). Constraints cannot be verified until variables and dependencies establish the reachable state space.
7. **Events** define state mutations and triggers. Events must operate on validated, constraint-compliant variables and respect dependency ordering.
8. **Evaluation Criteria** map simulation outputs to goal metrics. Defined last to ensure all outputs are known and measurable.

**Gate Enforcement:** Each stage must pass a validation checkpoint before proceeding. Skipping stages triggers hard compilation failure. This enforces _structured causal reasoning_ rather than ad-hoc parameter tweaking.

---

## II. Scenario Schema Design

The schema is the formal contract between the author and the engine. It is strictly typed, versioned, and machine-readable.

| Field Category            | Required/Optional        | Technical Description                                                        | Plain Explanation                                                       |
| ------------------------- | ------------------------ | ---------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| **`scenario_id`**         | Required                 | UUIDv5 derived from namespace + semantic name                                | Globally unique identifier for tracking and versioning                  |
| **`schema_version`**      | Required                 | Semantic version string (e.g., `1.2.0`)                                      | Ensures backward compatibility with engine and authoring tools          |
| **`master_seed`**         | Required                 | 64-bit unsigned integer                                                      | Root seed for all deterministic propagation and stochastic overlays     |
| **`assumptions`**         | Required                 | Array of string literals + validity predicates                               | Explicit bounds on what the model ignores or simplifies                 |
| **`entities`**            | Required                 | Map of entity descriptors with namespaces                                    | Containers that group related variables (e.g., `grid_cell`, `agent_01`) |
| **`variables`**           | Required                 | Array of typed state descriptors (dtype, unit, initial_value, domain_bounds) | The actual numbers/flags that change during simulation                  |
| **`dependency_edges`**    | Required                 | Array of directed relationships with propagation function IDs and weights    | How a change in one variable mathematically influences another          |
| **`constraints`**         | Required                 | Logical predicates (e.g., interval arithmetic rules)                         | Hard limits that prevent physically/logically impossible states         |
| **`events`**              | Optional but recommended | Trigger-condition + effect mappings with timeline/schedule                   | External or internal state changes that force system updates            |
| **`evaluation_criteria`** | Required                 | Metric definitions (aggregation functions, thresholds, comparison targets)   | How success/failure/probability is measured post-run                    |
| **`stochastic_overlays`** | Optional                 | Probabilistic distributions, confidence intervals, Monte Carlo parameters    | Controlled randomness applied _after_ deterministic core                |
| **`metadata`**            | Optional                 | Author tags, citations, revision notes, domain classification                | Human-readable context for collaboration and archival                   |

**Anti-Pattern Fields (Strictly Disallowed):**

- `implicit_state` / `hidden_variables`: Any value not explicitly declared breaks deterministic replay.
- `untyped_float` / `unitless_numbers`: Units must be explicit to prevent dimensional mismatch during propagation.
- `dynamic_dependency_generation` (without explicit rule triggers): Dependencies must be statically declared or conditionally activated via explicit schema rules, not runtime code injection.
- `unbounded_recursion_flag`: Allows infinite loops in dependency traversal. Rejected at validation.
- `global_side_effects`: Functions that modify state outside their declared scope. Breaks composability.

---

## III. Data Model & Engine Mapping

**Abstract Representation:**

- `ScenarioDefinition`: Immutable root object containing all schema components. Serialized as a canonical JSON/YAML or binary protobuf.
- `EntitySet`: Hierarchical namespace map. Each entity owns a contiguous block of variable descriptors.
- `VariableDescriptor`: `{ id, entity_ref, data_type, unit, initial_value, domain_min, domain_max, is_stochastic }`
- `DependencyEdge`: `{ source_handle, target_handle, propagation_fn_id, weight_matrix, activation_condition }`
- `ConstraintRule`: `{ predicate_expr, violation_policy (REJECT/CLAMP/WARN), evaluation_phase }`
- `EventDescriptor`: `{ trigger_expr, effect_patch, schedule (absolute/relative/conditional), priority }`

**Mapping to Core Engine:**
During compilation, the authoring system translates the schema into engine-native structures:

- `SimulationState` → SoA (Structure of Arrays) buffers allocated per data type. Variable descriptors map to column indices.
- `DependencyGraph` → Static adjacency lists. Edges are compiled to function pointers or JIT-compiled lambdas. Activation conditions become branch masks.
- `ConstraintEvaluator` → Interval arithmetic engine + lightweight SAT solver for pre-run feasibility checks.
- `EventScheduler` → Deterministic priority queue sorted by simulation timestamp and explicit priority weights.

This mapping is **one-way and irreversible**. The runtime state contains no schema metadata, ensuring zero memory overhead during execution.

---

## IV. Scenario Structuring

**Layering & Modularization:**

- **Base Layer**: Core physics, logic, and immutable dependencies. Validated first.
- **Context Layer**: Domain-specific overlays (e.g., economic policy, environmental stress). Activated conditionally.
- **Dynamic Overlay**: Runtime-adaptive edges added via trigger rules. Strictly bounded to prevent graph explosion.

**Subgraphs & Phases:**

- **Temporal Phasing**: Scenarios are partitioned into time windows. Each phase can load/unload subgraphs, enabling out-of-core execution.
- **Spatial/Logical Partitioning**: Large scenarios use disjoint subgraphs that only communicate through explicit interface variables (e.g., boundary conditions).

**Scope Management:**

- **Global**: Visible across all entities (e.g., `sim_time`, `master_seed`).
- **Local**: Entity-scoped (e.g., `agent.health`, `region.gdp`).
- **Contextual**: Active only when specific conditions are met (e.g., `crisis_mode.active = true`). Resolved via namespace prefixing to prevent collisions.

**Large-Scale Handling:**

- Hierarchical aggregation: Replace thousands of identical nodes with representative super-nodes + distribution descriptors.
- Lazy fragment loading: Subgraphs are memory-mapped and loaded only when their activation conditions trigger.
- Graph partitioning: Automated cut-finding algorithms split the dependency graph for multi-node execution while preserving boundary determinism.

---

## V. Authoring Interface

**Programmer-Facing C++ API:**

- Fluent builder with Compile-Time Type Checking (CTTC) using CRTP and `constexpr` validation.

```cpp
ScenarioBuilder::create("economic_shock_v1")
    .with_seed(0xDEADBEEF)
    .define_entity<Market>().variable("interest_rate", DataType::F64, unit::percent)
    .link_dependency("interest_rate" -> "credit_supply", PropFn::inverse_linear)
    .constrain("credit_supply > 0", Violation::REJECT)
    .compile();
```

- Compile-time guarantees: Type mismatches, unit inconsistencies, and missing references fail at build time, not runtime.

**Researcher-Friendly Abstraction:**

- Declarative configuration via schema-compliant files.
- Visual graph editor (optional) that outputs canonical schema files.

**Configuration Formats & Trade-offs:**
| Format | Pros | Cons | Recommended Use |
|--------|------|-----------------------|
| **JSON** | Ubiquitous, fast parsers, schema validation mature | Verbose, no native comments, strict syntax | Machine-generated scenarios, CI/CD pipelines |
| **YAML** | Human-readable, supports comments/anchors | Indentation-sensitive, ambiguous type coercion | Hand-authored research scenarios |
| **Custom DSL** | Type-safe, explicit validation, compact | Requires parser, learning curve, tooling overhead | Complex scenarios with advanced logic/rules |

_Recommendation:_ Use YAML for authoring, transpile to canonical JSON/protobuf for validation and engine ingestion.

---

## VI. Validation System

Multi-stage validation ensures scenarios are mathematically sound before execution.

1. **Schema Validation**: JSON Schema / Protobuf schema compliance. Checks types, required fields, value ranges.
2. **Semantic Validation**: Unit consistency, dimensional analysis, physical/logical plausibility (e.g., no negative mass, probability ∈ [0,1]).
3. **Dependency Validation**:
   - DAG verification (topological sort). Cycles in static graph → **REJECT**.
   - Orphan detection (variables with no incoming/outgoing edges unless explicitly flagged as terminals).
   - Fan-out cap warning (>100 dependents per node).
4. **Constraint Satisfiability**: Interval arithmetic + lightweight SAT solver to verify initial state meets all constraints. Infeasibility → **REJECT**.
5. **Ambiguity Detection**: Overlapping trigger conditions, duplicate variable names across namespaces, conflicting event schedules. → **WARN** or **REJECT** based on policy.

**Rejection vs Warning vs Auto-Correction Policy:**
| Issue Type | Action | Rationale |
|------------|--------|-----------|
| Type mismatch, missing reference, cycle in static graph, unsatisfiable constraint | **REJECT** | Fatal to determinism or correctness |
| High fan-out, near-boundary values, implicit casting | **WARN** | Performance or stability risk, but executable |
| Unit mismatch (e.g., `m` vs `cm`), seed normalization, graph topological sort | **AUTO-CORRECT** | Resolvable without author intervention; logged explicitly |

All validation outputs are serialized to a `ScenarioReport` artifact attached to the lifecycle state.

---

## VII. Scenario Lifecycle

Strict state machine with immutable transitions.

1. **Draft**: Unvalidated, mutable schema. Author iterates.
2. **Validated**: Passes all validation gates. Schema frozen. Hash computed (SHA-256). Ready for compilation.
3. **Runnable**: Compiled into engine state. Deterministic replay log enabled. Can be executed.
4. **Calibrated**: Parameters adjusted via sensitivity analysis or Bayesian optimization. Calibration metadata recorded.
5. **Versioned**: Cryptographic signature applied. Published to registry. Immutable.
6. **Archived**: Long-term storage with dependency graph snapshots, calibration history, and evaluation metrics.

**Transition Gates:** No backward transitions. Calibration requires re-compilation but preserves original hash for traceability. Versioning locks the schema; any modification creates a new version.

---

## VIII. Reusability & Composition

**Scenario Fragments:**

- Self-contained sub-scenarios with explicit interfaces (input/output variables).
- Imported via `include` directive. Validated independently before merge.

**Inheritance & Overrides:**

- Base scenario + delta patches (JSON Patch / RFC 7396).
- Override rules are explicit: `replace`, `append`, `merge`. No silent overwrites.

**Modular Composition:**

- Namespace isolation: Fragments use prefixes to prevent collisions.
- Conflict resolution: Priority-based (last-wins) with explicit override logging. Composition graph is validated for consistency before compilation.

**Template System:**

- Parameterized macros (`{{PARAM_NAME}}`) resolved at compile time.
- Type-safe instantiation: Templates enforce variable types and constraint compatibility.
- Enables parameter sweeping without duplicating schema files.

---

## IX. Integration with Core Engine

**Compilation Pipeline:**
`ScenarioCompiler` transforms validated schema into engine artifacts:

- Allocates contiguous SoA buffers.
- Resolves `ParameterHandle`s and populates `DependencyGraph` adjacency lists.
- Compiles propagation functions to native code or binds to pre-registered function registry.
- Populates `EventQueue` with deterministic priority ordering.

**Parameter & Seed Binding:**

- Centralized `SeedOrchestrator` derives deterministic PRNG streams per variable/component using `master_seed` + graph position hash.
- No seed state lives in parameters. Seeds are injected deterministically during state initialization.

**Event Scheduling Integration:**

- Declarative events transpiled to engine's timeline dispatcher.
- Deterministic tie-breaking via explicit priority weights and lexicographic handle ordering.
- External events logged to replay buffer with exact timestamp and source ID.

---

## X. Performance Considerations in Authoring

Authoring decisions directly impact runtime efficiency. The system enforces architectural guardrails:

**Prevent Degradation:**

- Fan-out/in caps: Warn if node degree > threshold. Reject if > hard limit.
- Graph depth limits: Prevent excessively long dependency chains that cause cache thrashing during pull recomputation.
- Enforce sparsity: Dense matrices discouraged for dependency propagation unless explicitly marked for SIMD/GPU offload.

**Avoid Dependency Explosion:**

- Hierarchical aggregation for large populations (e.g., replace 10,000 identical agents with distribution descriptors).
- Conditional edge activation: Dependencies remain dormant until trigger conditions fire.
- Subgraph partitioning: Independent regions compiled into separate execution contexts.

**Memory & Caching Implications:**

- Pre-allocated SoA buffers aligned to cache lines (64B) and SIMD boundaries (32B/64B).
- Memory-mapped scenario files for lazy loading.
- Validation results cached via schema hash to avoid redundant recomputation across runs.
- Compile-time graph flattening: Cyclic overlays converted to DAG equivalents where possible via iterative unrolling.

---

## XI. Failure Modes & Design Risks

| Failure Mode                              | Impact                                                | Detection & Mitigation                                                                                                |
| ----------------------------------------- | ----------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| **Circular Dependencies in Static Graph** | Infinite propagation loops, stack overflow            | Topological sort at validation. **REJECT** if cycle detected.                                                         |
| **Constraint Infeasibility**              | Simulation halts or produces NaN/inf                  | Interval arithmetic + SAT pre-check. **REJECT** if initial state violates bounds.                                     |
| **Composition Conflicts**                 | Overlapping variable names, contradictory constraints | Namespace isolation + explicit override policy. **REJECT** if unresolved.                                             |
| **Stochastic Leakage**                    | Non-deterministic runs despite identical seeds        | Centralized PRNG stream isolation. Seed mutation forbidden post-compilation. **REJECT** if hidden RNG calls detected. |
| **Graph Depth / Fan-out Explosion**       | Cache thrashing, pull recomputation latency spikes    | Degree/depth limits, hierarchical aggregation, partitioning. **WARN** + auto-optimize or **REJECT**.                  |
| **Implicit State Mutation**               | Replay divergence, untestable behavior                | Static analysis forbids side-effects. **REJECT** if function signature modifies non-target handles.                   |

All failures are logged to `ScenarioReport` with precise location, root cause, and remediation steps.

---

## XII. Design Critique: The "Embedded Seed Per Parameter" Paradigm

**Why It Fails Architecturally:**
Embedding seed logic directly into parameters violates separation of concerns, destroys composability, and fundamentally undermines determinism in the authoring context:

1. **Implicit Coupling**: If seeds are hidden inside variables, the dependency graph becomes opaque. Validation cannot verify propagation paths because seed derivation rules are scattered across parameter definitions.
2. **Composition Fragility**: Merging scenarios requires reconciling conflicting seed derivation rules. This leads to non-deterministic behavior when seeds mutate differently across composed subgraphs.
3. **Replay Divergence**: Embedded seeds that depend on runtime state (e.g., `seed = f(current_value)`) break bitwise reproducibility because floating-point precision and execution order variations alter seed derivation.
4. **Debugging Impossibility**: Isolating which seed caused a state divergence becomes intractable. Researchers cannot trace propagation chains without inspecting internal parameter implementations.

**Recommended Alternative: Centralized Seed Orchestration**

- **Master Seed + Derivation Graph**: A single `master_seed` initializes a deterministic PRNG. Seed streams are derived explicitly using `f(master_seed, component_id, version_hash)`.
- **Explicit Propagation Rules**: Mathematical influence is encoded in dependency edges, not parameter internals. Stochasticity is injected via explicit `stochastic_overlays` with declared distributions and sampling schedules.
- **Authoring Transparency**: The schema contains all seed derivation metadata. Researchers can visualize, validate, and modify seed propagation as a first-class construct, not a hidden implementation detail.
- **Deterministic Guarantee**: Identical schema + identical seed + identical execution path = bitwise identical outputs, regardless of scenario size or composition depth.

This approach preserves the "butterfly effect" concept through explicit mathematical propagation while guaranteeing scientific rigor, composability, and reproducible replay.

---

**Conclusion:** The Scenario Authoring System is not a front-end for the simulation engine; it is a formal verification and compilation pipeline that enforces structured, deterministic, and scalable scenario design. By separating definition from runtime state, enforcing strict validation gates, and centralizing seed orchestration, the system transforms ad-hoc parameter tuning into a rigorous, research-grade construction framework capable of supporting everything from small analytical models to large-scale, reality-grounded forecasting simulations.
