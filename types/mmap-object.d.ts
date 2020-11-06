// Types for package https://www.npmjs.com/package/mmap-object

declare module 'mmap-object' {

	type AccesibleObject = {
		[key: string]: any;
	};

	export class Base<T extends AccesibleObject> extends Object {
		/**
		 * Unmaps a previously created or opened file. If the file was most recently opened with Create(),
		 * close() will first shrink the file to remove any unneeded space that may have been allocated.
		 * It's important to close your unused shared files in long-running processes. Not doing so keeps shared memory from being freed.
		 * The closing of very large objects (a few gigabytes and up) may take some time (hundreds to thousands of milliseconds).
		 * To prevent blocking the main thread, pass a callback to close(). The call to close() will return immediately
		 * while the callback will be called after the underlying munmap() operation completes.
		 * Any error will be given as the first argument to the callback.
		 */
		close(callback?: (error: Error) => void): void;
		/**
		 * When iterating, use isData() to tell if a particular key is real data or one of the underlying methods on the shared object
		 */
		isData(key: keyof T): boolean;
		/**
		 * Return true if this object is currently open.
		 */
		isOpen(): boolean;
		/**
		 * Return true if this object has been closed.
		 */
		isClosed(): boolean;
		/**
		 * Number of bytes of free storage left in the shared object file.
		 */
		get_free_memory(): number;
		/**
		 * The size of the storage in the shared object file, in bytes.
		 */
		get_size(): number;
		/**
		 * The number of buckets currently allocated in the underlying hash structure.
		 */
		bucket_count(): number;
		/**
		 * The maximum number of buckets that can be allocated in the underlying hash structure.
		 */
		max_bucket_count(): number;
		/**
		 * The average number of elements per bucket.
		 */
		load_factor(): number;
		/**
		 * The current maximum load factor.
		 */
		max_load_factor(): number;
		[Symbol.iterator](): IterableIterator<T[keyof T]>;
	}

	/**
	 * Creates a new file mapped into shared memory.
	 * Returns an object that provides access to the shared memory.
	 * Throws an exception on error.
	 */
	export class Create<T> extends Base<T> {
		/**
		 * @param filePath The path of the file to create
		 * @param fileSize The initial size of the file in kilobytes. If more space is needed, the file will automatically be grown to a larger size. Minimum is 500 bytes. Defaults to 5 megabytes.
		 * @param initialBucketCount The number of buckets to allocate initially. This is passed to the underlying Boost unordered_map. Defaults to 1024. Set this to the number of keys you expect to write.
		 * @param maxFileSize The largest the file is allowed to grow in kilobites. If data is added beyond this limit, an exception is thrown. Defaults to 5 gigabytes.
		 */
		constructor(
			filePath: string,
			fileSize?: number,
			initialBucketCount?: number,
			maxFileSize?: number,
		);

		[key: string]: T[keyof T] | Base<T>[keyof Base<T>];
	}

	/**
	 * Maps an existing file into shared memory.
	 * Returns an object that provides read-only access to the object contained in the file.
	 * Throws an exception on error.
	 * Any number of processes can open the same file but only a single copy will reside in memory.
	 * Uses mmap under the covers, so only those parts of the file that are actually accessed will be loaded.
	 */
	export class Open<T> extends Base<T> {
		/**
		 * @param filePath The path of the file to open
		 */
		constructor(
			filePath: string,
		);

		[key: string]: T[keyof T] | Base<T>[keyof Base<T>];
	}
}
